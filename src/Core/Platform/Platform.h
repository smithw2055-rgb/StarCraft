#pragma once

#include <cstdint>

namespace Core::Platform
{
    namespace KeyCode
    {
        constexpr int Escape = 256;
        constexpr int F6 = 295;
        constexpr int F9 = 298;
    }

    bool Initialize();
    void Shutdown();
    void HandleEvent(const void* Event);
    void RequestQuit();

    // Call once per frame BEFORE game logic to flush deltas and update prev-key state
    void ProcessInput();

    bool QuitRequested();

    // Window
    int GetWindowWidth();
    int GetWindowHeight();
    int GetClientWidth();
    int GetClientHeight();
    int GetFramebufferWidth();
    int GetFramebufferHeight();
    float GetAspect();
    bool IsWindowFocused();
    bool IsMouseInClientArea();
    bool IsMouseInGameContentArea();

    // Mouse in logical window pixels, top-left origin.
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

    // Set the per-frame delta externally (driven by the backend's frame_duration)
    void SetDeltaTime(float Dt);
}
