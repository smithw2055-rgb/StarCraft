#pragma once

#include <cstdint>

struct sapp_event;

namespace Core::Platform
{
    bool Initialize();
    void Shutdown();
    void HandleEvent(const sapp_event* Event);

    // Call once per frame BEFORE game logic to flush deltas and update prev-key state
    void ProcessInput();

    bool QuitRequested();

    // Window
    int GetWindowWidth();
    int GetWindowHeight();
    float GetAspect();
    bool IsWindowFocused();
    bool IsMouseInClientArea();

    // Mouse (logical window pixels, top-left origin; DPI scaling removed)
    bool IsMouseButtonDown(int Button);
    bool IsMouseButtonPressed(int Button);
    bool IsMouseButtonReleased(int Button);
    float GetMouseX();
    float GetMouseY();
    float GetMouseDeltaX();
    float GetMouseDeltaY();
    float GetMouseWheel();
    void SetMouseCursorVisible(bool Visible);
    void SetMouseLocked(bool Locked);

    // Keyboard
    bool IsKeyDown(int KeyCode);
    bool IsKeyPressed(int KeyCode);
    bool IsKeyReleased(int KeyCode);

    // Frame timing
    float GetDeltaTime();
    double GetAbsoluteTime();

    // Set the per-frame delta externally (driven by sokol's frame_duration)
    void SetDeltaTime(float Dt);
}
