#include "imgui.h"
#include "imgui_engine.h"
#include "gl_local.h"

extern "C" void ImGui_Engine_Init(void) {}
extern "C" void ImGui_Engine_Shutdown(void) {}
extern "C" void ImGui_Engine_NewFrame(void) {}
extern "C" void ImGui_Engine_Render(void) {}
extern "C" int ImGui_Engine_WantCaptureMouse(void) { return 0; }
extern "C" int ImGui_Engine_WantCaptureKeyboard(void) { return 0; }
extern "C" void ImGui_Engine_DrawText(int x, int y, int r, int g, int b, int a, const char *text) {}
extern "C" int ImGui_Engine_LoadFont(const char *fontPath, float fontSize)
{
    gEngfuncs.Con_Printf("ImGui: LoadFont stub called for \"%s\" size %.0f; current renderer does not support ImGui fonts\n",
        fontPath ? fontPath : "(null)", fontSize);
    return 0;
}
extern "C" int ImGui_Engine_GetTextWidth(const char *text, float fontSize) { return 0; }
extern "C" void ImGui_Engine_SetScreenSize(int width, int height) {}
extern "C" int ImGui_Engine_LoadFontHandle(const char *fontPath, float fontSize) { return 0; }
extern "C" void ImGui_Engine_DrawTextFont(int fontHandle, int x, int y, int r, int g, int b, int a, const char *text, float fontSize) {}
extern "C" int ImGui_Engine_GetTextWidthFont(int fontHandle, const char *text, float fontSize) { return 0; }
extern "C" int ImGui_Engine_Begin(const char *name, int flags) { return 0; }
extern "C" void ImGui_Engine_End(void) {}
extern "C" void ImGui_Engine_Text(const char *text) {}
extern "C" void ImGui_Engine_TextColored(int r, int g, int b, int a, const char *text) {}
extern "C" int ImGui_Engine_Button(const char *label, float width, float height) { return 0; }
extern "C" int ImGui_Engine_Checkbox(const char *label, int *value) { return 0; }
extern "C" int ImGui_Engine_SliderFloat(const char *label, float *value, float minValue, float maxValue, const char *format) { return 0; }
extern "C" void ImGui_Engine_SetNextWindowPos(float x, float y, int cond) {}
extern "C" void ImGui_Engine_SetNextWindowSize(float width, float height, int cond) {}
extern "C" void ImGui_Engine_SetCursorPos(float x, float y) {}
extern "C" void ImGui_Engine_SameLine(float offsetFromStartX, float spacing) {}
extern "C" void ImGui_Engine_Separator(void) {}
extern "C" void ImGui_Engine_Spacing(void) {}
extern "C" int ImGui_Engine_BeginChild(const char *id, float width, float height, int border, int flags) { return 0; }
extern "C" void ImGui_Engine_EndChild(void) {}
