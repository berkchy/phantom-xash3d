/*
 * imgui_engine.cpp - ImGui integration for Xash3D engine
 */

#include "imgui_engine.h"
#include "imgui.h"
#include "imgui_impl_opengl3.h"

#include "gl_local.h"
#include <float.h>

static int g_initialized = 0;
static int g_inside_frame = 0;
static char g_loadedFontPath[MAX_QPATH];
static float g_loadedFontSize = 0.0f;
static float g_drawFontSize = 0.0f;
static ImFont *g_loadedFont = NULL;

#define IMGUI_ENGINE_MAX_FONT_HANDLES 16

typedef struct imgui_font_slot_s
{
    char path[MAX_QPATH];
    float size;
    ImFont *font;
    int pending;
} imgui_font_slot_t;

static imgui_font_slot_t g_fontSlots[IMGUI_ENGINE_MAX_FONT_HANDLES];

static void ImGui_Log(const char *fmt, ...);
static void ImGui_FinishFrame(const char *reason);
static void ImGui_ClearFontHandles(void);
static int ImGui_LoadFontHandleSlot(int slot);
static void ImGui_ProcessPendingFontHandles(void);

static unsigned short ImGui_ReadBE16(const byte *p)
{
    return (unsigned short)((p[0] << 8) | p[1]);
}

static unsigned int ImGui_ReadBE32(const byte *p)
{
    return ((unsigned int)p[0] << 24) | ((unsigned int)p[1] << 16) | ((unsigned int)p[2] << 8) | p[3];
}

static void ImGui_CopyNameString(char *out, size_t outSize, const byte *data, int len, int utf16be)
{
    size_t oi = 0;
    if (!out || outSize == 0)
        return;

    out[0] = '\0';
    if (!data || len <= 0)
        return;

    if (utf16be)
    {
        for (int i = 0; i + 1 < len && oi + 1 < outSize; i += 2)
        {
            unsigned short ch = ImGui_ReadBE16(data + i);
            out[oi++] = (ch >= 32 && ch < 127) ? (char)ch : '?';
        }
    }
    else
    {
        for (int i = 0; i < len && oi + 1 < outSize; i++)
        {
            unsigned char ch = data[i];
            out[oi++] = (ch >= 32 && ch < 127) ? (char)ch : '?';
        }
    }

    out[oi] = '\0';
}

static void ImGui_SetNameIfBetter(char *dst, size_t dstSize, const char *src, int platform, int language)
{
    if (!dst || !src || !src[0])
        return;

    if (!dst[0] || (platform == 3 && language == 0x0409))
        Q_strncpy(dst, src, dstSize);
}

static void ImGui_LogTtfInfo(const char *fontPath, const byte *data, fs_offset_t size)
{
    if (!data || size < 12)
    {
        ImGui_Log("LoadFont: TTF inspect skipped - file too small for sfnt header\n");
        return;
    }

    unsigned int sfnt = ImGui_ReadBE32(data);
    unsigned short numTables = ImGui_ReadBE16(data + 4);

    ImGui_Log("LoadFont: TTF header for \"%s\": sfnt=0x%08X numTables=%u\n",
        fontPath ? fontPath : "(null)", sfnt, numTables);

    if (size < 12 + (fs_offset_t)numTables * 16)
    {
        ImGui_Log("LoadFont: TTF inspect warning - table directory exceeds file size\n");
        return;
    }

    char tableList[512];
    size_t tableLen = 0;
    tableList[0] = '\0';

    fs_offset_t nameOffset = 0;
    fs_offset_t nameLength = 0;

    for (unsigned int i = 0; i < numTables; i++)
    {
        const byte *rec = data + 12 + i * 16;
        char tag[5];
        tag[0] = (char)rec[0];
        tag[1] = (char)rec[1];
        tag[2] = (char)rec[2];
        tag[3] = (char)rec[3];
        tag[4] = '\0';

        if (tableLen + 6 < sizeof(tableList))
        {
            int written = Q_snprintf(tableList + tableLen, sizeof(tableList) - tableLen,
                "%s%s", tableLen ? "," : "", tag);
            if (written > 0)
                tableLen += (size_t)written;
        }

        if (!Q_strncmp(tag, "name", 4))
        {
            nameOffset = (fs_offset_t)ImGui_ReadBE32(rec + 8);
            nameLength = (fs_offset_t)ImGui_ReadBE32(rec + 12);
        }
    }

    ImGui_Log("LoadFont: TTF tables: %s\n", tableList[0] ? tableList : "(none)");

    if (nameOffset <= 0 || nameLength <= 0 || nameOffset + nameLength > size || nameLength < 6)
    {
        ImGui_Log("LoadFont: TTF name table unavailable or out of range (offset=%lld length=%lld)\n",
            (long long)nameOffset, (long long)nameLength);
        return;
    }

    const byte *name = data + nameOffset;
    unsigned short count = ImGui_ReadBE16(name + 2);
    unsigned short stringOffset = ImGui_ReadBE16(name + 4);

    if (6 + (fs_offset_t)count * 12 > nameLength || stringOffset >= nameLength)
    {
        ImGui_Log("LoadFont: TTF name table invalid (count=%u stringOffset=%u length=%lld)\n",
            count, stringOffset, (long long)nameLength);
        return;
    }

    char family[128] = "";
    char subfamily[128] = "";
    char fullName[128] = "";
    char postscript[128] = "";

    for (unsigned int i = 0; i < count; i++)
    {
        const byte *rec = name + 6 + i * 12;
        unsigned short platform = ImGui_ReadBE16(rec + 0);
        unsigned short language = ImGui_ReadBE16(rec + 4);
        unsigned short nameId = ImGui_ReadBE16(rec + 6);
        unsigned short len = ImGui_ReadBE16(rec + 8);
        unsigned short off = ImGui_ReadBE16(rec + 10);

        if ((fs_offset_t)stringOffset + off + len > nameLength)
            continue;

        char value[128];
        ImGui_CopyNameString(value, sizeof(value), name + stringOffset + off, len, platform == 0 || platform == 3);

        switch (nameId)
        {
        case 1: ImGui_SetNameIfBetter(family, sizeof(family), value, platform, language); break;
        case 2: ImGui_SetNameIfBetter(subfamily, sizeof(subfamily), value, platform, language); break;
        case 4: ImGui_SetNameIfBetter(fullName, sizeof(fullName), value, platform, language); break;
        case 6: ImGui_SetNameIfBetter(postscript, sizeof(postscript), value, platform, language); break;
        default: break;
        }
    }

    ImGui_Log("LoadFont: TTF names: family=\"%s\" subfamily=\"%s\" full=\"%s\" postscript=\"%s\"\n",
        family[0] ? family : "(unknown)",
        subfamily[0] ? subfamily : "(unknown)",
        fullName[0] ? fullName : "(unknown)",
        postscript[0] ? postscript : "(unknown)");
}

static void ImGui_Log(const char *fmt, ...)
{
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    gEngfuncs.Con_Printf("ImGui: %s", buf);

    if (gEngfuncs.fsapi)
    {
        file_t *logFile = gEngfuncs.fsapi->Open("imgui.log", "a", false);
        if (logFile)
        {
            gEngfuncs.fsapi->Printf(logFile, "%s", buf);
            gEngfuncs.fsapi->Close(logFile);
        }
    }
}

static void ImGui_ClearFontHandles(void)
{
    memset(g_fontSlots, 0, sizeof(g_fontSlots));
}

static ImFont *ImGui_GetFontHandle(int fontHandle)
{
    if (fontHandle <= 0 || fontHandle > IMGUI_ENGINE_MAX_FONT_HANDLES)
        return NULL;

    return g_fontSlots[fontHandle - 1].font;
}

static int ImGui_FindFontSlot(const char *fontPath, float fontSize)
{
    for (int i = 0; i < IMGUI_ENGINE_MAX_FONT_HANDLES; i++)
    {
        if ((g_fontSlots[i].font || g_fontSlots[i].pending) &&
            !Q_stricmp(g_fontSlots[i].path, fontPath) && g_fontSlots[i].size == fontSize)
            return i;
    }

    return -1;
}

static int ImGui_FindFreeFontSlot(void)
{
    for (int i = 0; i < IMGUI_ENGINE_MAX_FONT_HANDLES; i++)
    {
        if (!g_fontSlots[i].font && !g_fontSlots[i].pending)
            return i;
    }

    return -1;
}

static void EnsureInit(void)
{
    if (g_initialized)
        return;

    ImGui_Log("EnsureInit: creating ImGui context\n");

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    io.Fonts->AddFontDefault();
    io.Fonts->Build();

    ImGui_Log("EnsureInit: default font atlas built (%d fonts, io.FontDefault=%p)\n",
        io.Fonts->Fonts.Size, (void*)io.FontDefault);

    if (io.Fonts->Fonts.Size > 0 && io.Fonts->Fonts[0])
        ImGui_Log("EnsureInit: fallback font name=\"%s\" size=%.0f\n",
            io.Fonts->Fonts[0]->GetDebugName(), io.Fonts->Fonts[0]->FontSize);

    if (!ImGui_ImplOpenGL3_Init("#version 100"))
    {
        ImGui_Log("EnsureInit: OpenGL3 backend init FAILED\n");
        ImGui::DestroyContext();
        return;
    }

    g_initialized = 1;
    ImGui_Log("EnsureInit: initialized successfully\n");
}

extern "C" void ImGui_Engine_Init(void)
{
    EnsureInit();
}

extern "C" void ImGui_Engine_Shutdown(void)
{
    if (!g_initialized) return;
    ImGui_ImplOpenGL3_Shutdown();
    ImGui::DestroyContext();
    g_initialized = 0;
    g_loadedFontPath[0] = '\0';
    g_loadedFontSize = 0.0f;
    g_drawFontSize = 0.0f;
    g_loadedFont = NULL;
    ImGui_ClearFontHandles();
}

extern "C" void ImGui_Engine_NewFrame(void)
{
    EnsureInit();
    if (!g_initialized) return;

    if (g_inside_frame)
    {
        return;
    }

    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)gpGlobals->width, (float)gpGlobals->height);

    ImGui_ProcessPendingFontHandles();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();
    g_inside_frame = 1;
}

static void ImGui_FinishFrame(const char *reason)
{
    if (!g_initialized) return;
    if (!g_inside_frame) return;

    g_inside_frame = 0;
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    if (reason && reason[0] && Q_stricmp(reason, "R_EndFrame"))
        ImGui_Log("Render: finished frame (%s)\n", reason);
}

extern "C" void ImGui_Engine_Render(void)
{
    ImGui_FinishFrame("R_EndFrame");
}

extern "C" int ImGui_Engine_WantCaptureMouse(void)
{
    return (g_initialized && ImGui::GetCurrentContext()) ? ImGui::GetIO().WantCaptureMouse : 0;
}

extern "C" int ImGui_Engine_WantCaptureKeyboard(void)
{
    return (g_initialized && ImGui::GetCurrentContext()) ? ImGui::GetIO().WantCaptureKeyboard : 0;
}

/* Drawing functions for menu DLL */
extern "C" void ImGui_Engine_DrawText(int x, int y, int r, int g, int b, int a, const char *text)
{
    if (!g_initialized || !text || !text[0]) return;
    ImDrawList *drawList = ImGui::GetBackgroundDrawList();
    if (!drawList) return;

    if (g_loadedFont && g_drawFontSize > 0.0f)
        drawList->AddText(g_loadedFont, g_drawFontSize, ImVec2((float)x, (float)y), IM_COL32(r, g, b, a), text);
    else
        drawList->AddText(ImVec2((float)x, (float)y), IM_COL32(r, g, b, a), text);
}

extern "C" int ImGui_Engine_LoadFont(const char *fontPath, float fontSize)
{
    ImGui_Log("LoadFont: path=\"%s\" size=%.0f\n",
        fontPath ? fontPath : "(null)", fontSize);

    if (!fontPath || !fontPath[0])
    {
        ImGui_Log("LoadFont: FAILED - empty font path\n");
        return 0;
    }
    if (fontSize <= 0)
    {
        ImGui_Log("LoadFont: FAILED - invalid font size %.0f\n", fontSize);
        return 0;
    }

    if (!gEngfuncs.fsapi)
    {
        ImGui_Log("LoadFont: FAILED - fsapi is NULL\n");
        return 0;
    }

    if (g_loadedFont && !Q_stricmp(g_loadedFontPath, fontPath) && g_loadedFontSize == fontSize)
    {
        if (g_initialized && ImGui::GetCurrentContext())
            ImGui::GetIO().FontDefault = g_loadedFont;

        ImGui_Log("LoadFont: already loaded \"%s\" size=%.0f, reusing existing font=%p\n",
            g_loadedFontPath, g_loadedFontSize, (void*)g_loadedFont);
        return 1;
    }

    char rootPath[1024] = "";
    if (gEngfuncs.fsapi->GetRootDirectory && gEngfuncs.fsapi->GetRootDirectory(rootPath, sizeof(rootPath)))
        ImGui_Log("LoadFont: filesystem root=\"%s\"\n", rootPath);
    else
        ImGui_Log("LoadFont: filesystem root unavailable\n");

    ImGui_Log("LoadFont: filesystem gamedir=\"%s\"\n",
        gEngfuncs.fsapi->Gamedir ? gEngfuncs.fsapi->Gamedir() : "(unavailable)");

    int existsAny = gEngfuncs.fsapi->FileExists ? gEngfuncs.fsapi->FileExists(fontPath, false) : -1;
    int existsGame = gEngfuncs.fsapi->FileExists ? gEngfuncs.fsapi->FileExists(fontPath, true) : -1;
    fs_offset_t reportedSizeAny = gEngfuncs.fsapi->FileSize ? gEngfuncs.fsapi->FileSize(fontPath, false) : -1;
    fs_offset_t reportedSizeGame = gEngfuncs.fsapi->FileSize ? gEngfuncs.fsapi->FileSize(fontPath, true) : -1;

    ImGui_Log("LoadFont: FileExists(\"%s\", any)=%d FileExists(gamedironly)=%d\n",
        fontPath, existsAny, existsGame);
    ImGui_Log("LoadFont: FileSize(\"%s\", any)=%lld FileSize(gamedironly)=%lld\n",
        fontPath, (long long)reportedSizeAny, (long long)reportedSizeGame);

    if (gEngfuncs.fsapi->GetDiskPath)
    {
        const char *diskAny = gEngfuncs.fsapi->GetDiskPath(fontPath, false);
        const char *diskGame = gEngfuncs.fsapi->GetDiskPath(fontPath, true);
        ImGui_Log("LoadFont: GetDiskPath(any)=\"%s\"\n", diskAny ? diskAny : "(null: archive or not found)");
        ImGui_Log("LoadFont: GetDiskPath(gamedironly)=\"%s\"\n", diskGame ? diskGame : "(null: archive or not found)");
    }

    if (gEngfuncs.fsapi->Open)
    {
        file_t *probe = gEngfuncs.fsapi->Open(fontPath, "rb", false);
        if (probe)
        {
            ImGui_Log("LoadFont: Open(\"%s\", any) succeeded length=%lld archive=\"%s\"\n",
                fontPath,
                gEngfuncs.fsapi->FileLength ? (long long)gEngfuncs.fsapi->FileLength(probe) : -1LL,
                gEngfuncs.fsapi->ArchivePath ? gEngfuncs.fsapi->ArchivePath(probe) : "(unavailable)");
            gEngfuncs.fsapi->Close(probe);
        }
        else
        {
            ImGui_Log("LoadFont: Open(\"%s\", any) failed\n", fontPath);
        }
    }

    EnsureInit();
    if (!g_initialized)
    {
        ImGui_Log("LoadFont: FAILED - ImGui not initialized\n");
        return 0;
    }

    ImGuiIO &io = ImGui::GetIO();
    if (!io.Fonts)
    {
        ImGui_Log("LoadFont: FAILED - io.Fonts is NULL\n");
        return 0;
    }

    if (g_loadedFont && !Q_stricmp(g_loadedFontPath, fontPath) && g_loadedFontSize == fontSize)
    {
        io.FontDefault = g_loadedFont;
        ImGui_Log("LoadFont: already loaded \"%s\" size=%.0f after init, reusing existing font=%p\n",
            g_loadedFontPath, g_loadedFontSize, (void*)g_loadedFont);
        return 1;
    }

    int fontCount = io.Fonts->Fonts.Size;
    ImGui_Log("LoadFont: font atlas has %d fonts before loading\n", fontCount);
    if (io.FontDefault)
        ImGui_Log("LoadFont: current io.FontDefault=%s size=%.0f\n",
            io.FontDefault->GetDebugName(), io.FontDefault->FontSize);
    else if (fontCount > 0 && io.Fonts->Fonts[0])
        ImGui_Log("LoadFont: current fallback font=%s size=%.0f\n",
            io.Fonts->Fonts[0]->GetDebugName(), io.Fonts->Fonts[0]->FontSize);

    fs_offset_t fileSize = 0;
    byte *fileData = gEngfuncs.fsapi->LoadFileMalloc(fontPath, &fileSize, false);
    if (!fileData)
    {
        ImGui_Log("LoadFont: FAILED - LoadFileMalloc returned NULL for \"%s\"\n", fontPath);
        return 0;
    }
    if (fileSize <= 0)
    {
        ImGui_Log("LoadFont: FAILED - file \"%s\" is empty (size=%lld)\n", fontPath, (long long)fileSize);
        free(fileData);
        return 0;
    }

    ImGui_Log("LoadFont: loaded \"%s\" (%lld bytes)\n", fontPath, (long long)fileSize);
    ImGui_LogTtfInfo(fontPath, fileData, fileSize);

    io.Fonts->Clear();
    io.FontDefault = NULL;
    g_loadedFontPath[0] = '\0';
    g_loadedFontSize = 0.0f;
    g_drawFontSize = 0.0f;
    g_loadedFont = NULL;
    ImGui_ClearFontHandles();
    ImGui_Log("LoadFont: cleared font atlas before adding custom font\n");

    ImFontConfig fontConfig;
    fontConfig.FontDataOwnedByAtlas = true;
    ImFont *font = io.Fonts->AddFontFromMemoryTTF(fileData, (int)fileSize, fontSize, &fontConfig);
    if (!font)
    {
        ImGui_Log("LoadFont: FAILED - AddFontFromMemoryTTF returned NULL (invalid/corrupt TTF?)\n");
        free(fileData);
        return 0;
    }

    ImGui_Log("LoadFont: AddFontFromMemoryTTF succeeded, font=%p\n", (void*)font);

    io.FontDefault = font;
    ImGui_Log("LoadFont: io.FontDefault set to new font\n");

    Q_strncpy(g_loadedFontPath, fontPath, sizeof(g_loadedFontPath));
    g_loadedFontSize = fontSize;
    g_drawFontSize = fontSize;
    g_loadedFont = font;

    bool built = io.Fonts->Build();
    ImGui_Log("LoadFont: font atlas built=%s (%d fonts)\n",
        built ? "true" : "false", io.Fonts->Fonts.Size);

    ImGui_ImplOpenGL3_DestroyFontsTexture();
    ImGui_Log("LoadFont: destroyed old GPU texture (will recreate on next frame)\n");

    ImGui_Log("LoadFont: SUCCESS - font=\"%s\" size=%.0f\n", fontPath, fontSize);
    return 1;
}

static int ImGui_LoadFontHandleSlot(int slot)
{
    const char *fontPath = g_fontSlots[slot].path;
    float fontSize = g_fontSlots[slot].size;
    ImGuiIO &io = ImGui::GetIO();

    if (!fontPath[0] || fontSize <= 0.0f)
    {
        ImGui_Log("LoadFontHandle: FAILED - invalid slot=%d\n", slot + 1);
        return 0;
    }

    if (!io.Fonts || g_inside_frame || io.Fonts->Locked)
    {
        ImGui_Log("LoadFontHandle: slot=%d deferred - atlas is locked\n", slot + 1);
        g_fontSlots[slot].pending = 1;
        return 0;
    }

    fs_offset_t fileSize = 0;
    byte *fileData = gEngfuncs.fsapi->LoadFileMalloc(fontPath, &fileSize, false);
    if (!fileData || fileSize <= 0)
    {
        ImGui_Log("LoadFontHandle: FAILED - could not load \"%s\" (size=%lld)\n",
            fontPath, (long long)fileSize);
        if (fileData)
            free(fileData);
        g_fontSlots[slot].pending = 0;
        return 0;
    }

    ImGui_Log("LoadFontHandle: loaded \"%s\" (%lld bytes)\n", fontPath, (long long)fileSize);
    ImGui_LogTtfInfo(fontPath, fileData, fileSize);

    ImFontConfig fontConfig;
    fontConfig.FontDataOwnedByAtlas = true;
    ImFont *font = io.Fonts->AddFontFromMemoryTTF(fileData, (int)fileSize, fontSize, &fontConfig);
    if (!font)
    {
        ImGui_Log("LoadFontHandle: FAILED - AddFontFromMemoryTTF returned NULL\n");
        free(fileData);
        g_fontSlots[slot].pending = 0;
        return 0;
    }

    g_fontSlots[slot].font = font;
    g_fontSlots[slot].pending = 0;

    bool built = io.Fonts->Build();
    ImGui_ImplOpenGL3_DestroyFontsTexture();

    ImGui_Log("LoadFontHandle: SUCCESS - handle=%d font=%p built=%s atlasFonts=%d\n",
        slot + 1, (void*)font, built ? "true" : "false", io.Fonts->Fonts.Size);
    return 1;
}

static void ImGui_ProcessPendingFontHandles(void)
{
    if (!g_initialized || !gEngfuncs.fsapi)
        return;

    for (int i = 0; i < IMGUI_ENGINE_MAX_FONT_HANDLES; i++)
    {
        if (g_fontSlots[i].pending)
        {
            ImGui_Log("LoadFontHandle: processing pending handle=%d path=\"%s\" size=%.0f\n",
                i + 1, g_fontSlots[i].path, g_fontSlots[i].size);
            ImGui_LoadFontHandleSlot(i);
        }
    }
}

extern "C" int ImGui_Engine_LoadFontHandle(const char *fontPath, float fontSize)
{
    ImGui_Log("LoadFontHandle: path=\"%s\" size=%.0f\n",
        fontPath ? fontPath : "(null)", fontSize);

    if (!fontPath || !fontPath[0] || fontSize <= 0.0f)
    {
        ImGui_Log("LoadFontHandle: FAILED - invalid request\n");
        return 0;
    }

    if (!gEngfuncs.fsapi)
    {
        ImGui_Log("LoadFontHandle: FAILED - fsapi is NULL\n");
        return 0;
    }

    EnsureInit();
    if (!g_initialized)
    {
        ImGui_Log("LoadFontHandle: FAILED - ImGui not initialized\n");
        return 0;
    }

    ImGuiIO &io = ImGui::GetIO();
    if (!io.Fonts)
    {
        ImGui_Log("LoadFontHandle: FAILED - io.Fonts is NULL\n");
        return 0;
    }

    int slot = ImGui_FindFontSlot(fontPath, fontSize);
    if (slot >= 0)
    {
        ImGui_Log("LoadFontHandle: reusing handle=%d font=%p pending=%d for \"%s\" size=%.0f\n",
            slot + 1, (void*)g_fontSlots[slot].font, g_fontSlots[slot].pending, fontPath, fontSize);
        return slot + 1;
    }

    slot = ImGui_FindFreeFontSlot();
    if (slot < 0)
    {
        ImGui_Log("LoadFontHandle: FAILED - no free font handle slots\n");
        return 0;
    }

    Q_strncpy(g_fontSlots[slot].path, fontPath, sizeof(g_fontSlots[slot].path));
    g_fontSlots[slot].size = fontSize;
    g_fontSlots[slot].font = NULL;
    g_fontSlots[slot].pending = 0;

    if (g_inside_frame || io.Fonts->Locked)
    {
        g_fontSlots[slot].pending = 1;
        ImGui_Log("LoadFontHandle: reserved pending handle=%d because atlas is locked\n", slot + 1);
        return slot + 1;
    }

    if (!ImGui_LoadFontHandleSlot(slot))
        return 0;
    return slot + 1;
}

extern "C" int ImGui_Engine_GetTextWidth(const char *text, float fontSize)
{
    if (!g_initialized || !text || !text[0]) return 0;

    if (fontSize > 0.0f)
        g_drawFontSize = fontSize;
    else if (g_loadedFontSize > 0.0f)
        g_drawFontSize = g_loadedFontSize;

    if (g_loadedFont && g_drawFontSize > 0.0f)
        return (int)g_loadedFont->CalcTextSizeA(g_drawFontSize, FLT_MAX, 0.0f, text).x;

    return (int)ImGui::CalcTextSize(text).x;
}

extern "C" int ImGui_Engine_GetTextWidthFont(int fontHandle, const char *text, float fontSize)
{
    if (!g_initialized || !text || !text[0]) return 0;

    ImFont *font = ImGui_GetFontHandle(fontHandle);
    if (font)
    {
        float size = fontSize > 0.0f ? fontSize : font->FontSize;
        return (int)font->CalcTextSizeA(size, FLT_MAX, 0.0f, text).x;
    }

    return ImGui_Engine_GetTextWidth(text, fontSize);
}

extern "C" void ImGui_Engine_DrawTextFont(int fontHandle, int x, int y, int r, int g, int b, int a, const char *text, float fontSize)
{
    if (!g_initialized || !text || !text[0]) return;

    ImDrawList *drawList = ImGui::GetBackgroundDrawList();
    if (!drawList) return;

    ImFont *font = ImGui_GetFontHandle(fontHandle);
    if (font)
    {
        float size = fontSize > 0.0f ? fontSize : font->FontSize;
        drawList->AddText(font, size, ImVec2((float)x, (float)y), IM_COL32(r, g, b, a), text);
        return;
    }

    ImGui_Engine_DrawText(x, y, r, g, b, a, text);
}

extern "C" void ImGui_Engine_SetScreenSize(int width, int height)
{
    if (!g_initialized) return;
    ImGui::GetIO().DisplaySize = ImVec2((float)width, (float)height);
}

static bool ImGui_MenuReady(void)
{
    return g_initialized && ImGui::GetCurrentContext() && g_inside_frame;
}

extern "C" int ImGui_Engine_Begin(const char *name, int flags)
{
    if (!ImGui_MenuReady() || !name || !name[0]) return 0;
    return ImGui::Begin(name, NULL, (ImGuiWindowFlags)flags) ? 1 : 0;
}

extern "C" void ImGui_Engine_End(void)
{
    if (!ImGui_MenuReady()) return;
    ImGui::End();
}

extern "C" void ImGui_Engine_Text(const char *text)
{
    if (!ImGui_MenuReady() || !text) return;
    ImGui::TextUnformatted(text);
}

extern "C" void ImGui_Engine_TextColored(int r, int g, int b, int a, const char *text)
{
    if (!ImGui_MenuReady() || !text) return;
    ImGui::TextColored(ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f), "%s", text);
}

extern "C" int ImGui_Engine_Button(const char *label, float width, float height)
{
    if (!ImGui_MenuReady() || !label) return 0;
    return ImGui::Button(label, ImVec2(width, height)) ? 1 : 0;
}

extern "C" int ImGui_Engine_Checkbox(const char *label, int *value)
{
    if (!ImGui_MenuReady() || !label || !value) return 0;

    bool checked = *value != 0;
    bool changed = ImGui::Checkbox(label, &checked);
    *value = checked ? 1 : 0;
    return changed ? 1 : 0;
}

extern "C" int ImGui_Engine_SliderFloat(const char *label, float *value, float minValue, float maxValue, const char *format)
{
    if (!ImGui_MenuReady() || !label || !value) return 0;
    return ImGui::SliderFloat(label, value, minValue, maxValue, format && format[0] ? format : "%.3f") ? 1 : 0;
}

extern "C" void ImGui_Engine_SetNextWindowPos(float x, float y, int cond)
{
    if (!ImGui_MenuReady()) return;
    ImGui::SetNextWindowPos(ImVec2(x, y), (ImGuiCond)cond);
}

extern "C" void ImGui_Engine_SetNextWindowSize(float width, float height, int cond)
{
    if (!ImGui_MenuReady()) return;
    ImGui::SetNextWindowSize(ImVec2(width, height), (ImGuiCond)cond);
}

extern "C" void ImGui_Engine_SetCursorPos(float x, float y)
{
    if (!ImGui_MenuReady()) return;
    ImGui::SetCursorPos(ImVec2(x, y));
}

extern "C" void ImGui_Engine_SameLine(float offsetFromStartX, float spacing)
{
    if (!ImGui_MenuReady()) return;
    ImGui::SameLine(offsetFromStartX, spacing);
}

extern "C" void ImGui_Engine_Separator(void)
{
    if (!ImGui_MenuReady()) return;
    ImGui::Separator();
}

extern "C" void ImGui_Engine_Spacing(void)
{
    if (!ImGui_MenuReady()) return;
    ImGui::Spacing();
}

extern "C" int ImGui_Engine_BeginChild(const char *id, float width, float height, int border, int flags)
{
    if (!ImGui_MenuReady() || !id || !id[0]) return 0;
    return ImGui::BeginChild(id, ImVec2(width, height), border != 0, (ImGuiWindowFlags)flags) ? 1 : 0;
}

extern "C" void ImGui_Engine_EndChild(void)
{
    if (!ImGui_MenuReady()) return;
    ImGui::EndChild();
}
