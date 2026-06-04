/*
 * imgui_engine.h - ImGui integration for Xash3D engine
 */

#ifndef IMGUI_ENGINE_H
#define IMGUI_ENGINE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize ImGui in the engine (call from R_Init) */
void ImGui_Engine_Init(void);

/* Shutdown ImGui (call from R_Shutdown) */
void ImGui_Engine_Shutdown(void);

/* Start a new ImGui frame (call from V_PostRender, after R_Set2DMode) */
void ImGui_Engine_NewFrame(void);

/* Render ImGui draw data (call from V_PostRender, before R_EndFrame) */
void ImGui_Engine_Render(void);

/* Check if ImGui wants to capture input */
int ImGui_Engine_WantCaptureMouse(void);
int ImGui_Engine_WantCaptureKeyboard(void);

#ifdef __cplusplus
}
#endif

#endif /* IMGUI_ENGINE_H */
