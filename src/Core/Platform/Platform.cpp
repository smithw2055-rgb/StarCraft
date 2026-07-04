#include "Core/Platform/Platform.h"
#include "Core/Platform/MouseCoordinate.h"

#include "third_party/sokol/sokol_app.h"

#include <algorithm>
#include <cstring>
#include <cmath>

namespace
{
    bool g_initialized = false;
    bool g_quitRequested = false;

    int g_windowWidth = 1280;
    int g_windowHeight = 720;
    bool g_windowFocused = true;
    bool g_mouseInClientArea = true;

    bool g_mouseDown[3] = {false, false, false};
    bool g_mousePressed[3] = {false, false, false};
    bool g_mouseReleased[3] = {false, false, false};
    bool g_mousePressedPending[3] = {false, false, false};
    bool g_mouseReleasedPending[3] = {false, false, false};

    float g_mouseX = -1.0f;
    float g_mouseY = -1.0f;
    float g_mouseDX = 0.0f;
    float g_mouseDY = 0.0f;
    float g_mouseScroll = 0.0f;
    bool g_cursorVisible = true;
    bool g_cursorLocked = false;
    float g_mouseScaleX = 1.0f;
    float g_mouseScaleY = 1.0f;

    static constexpr int kKeyCount = 512;
    uint8_t g_keys[kKeyCount] = {};
    bool g_keysPressed[kKeyCount] = {};
    bool g_keysReleased[kKeyCount] = {};
    bool g_keysPressedPending[kKeyCount] = {};
    bool g_keysReleasedPending[kKeyCount] = {};

    double g_lastFrameTime = 0.0;
    double g_currentTime = 0.0;
    float g_deltaTime = 0.016f;

    void UpdateMouseScale(int WindowWidth, int WindowHeight, int FramebufferWidth, int FramebufferHeight)
    {
        const Core::Platform::MouseScale Scale =
            Core::Platform::ComputeMouseScale(
                WindowWidth, WindowHeight,
                FramebufferWidth, FramebufferHeight,
                sapp_dpi_scale());
        g_mouseScaleX = Scale.X;
        g_mouseScaleY = Scale.Y;
    }

    float MouseXToFramebuffer(float X) { return Core::Platform::MouseCoordToFramebuffer(X, g_mouseScaleX); }
    float MouseYToFramebuffer(float Y) { return Core::Platform::MouseCoordToFramebuffer(Y, g_mouseScaleY); }
}

namespace Core::Platform
{
    void HandleEvent(const void* EventPtr)
    {
        const sapp_event* Event = static_cast<const sapp_event*>(EventPtr);
        if (!Event) return;
        switch (Event->type)
        {
            case SAPP_EVENTTYPE_QUIT_REQUESTED:
                g_quitRequested = true;
                break;
            case SAPP_EVENTTYPE_RESIZED:
                if (Event->window_width > 0 && Event->window_height > 0)
                {
                    g_windowWidth = Event->framebuffer_width > 0 ? Event->framebuffer_width : Event->window_width;
                    g_windowHeight = Event->framebuffer_height > 0 ? Event->framebuffer_height : Event->window_height;
                    UpdateMouseScale(Event->window_width, Event->window_height,
                                     Event->framebuffer_width, Event->framebuffer_height);
                }
                else
                {
                    g_windowWidth = std::max(1, Event->framebuffer_width);
                    g_windowHeight = std::max(1, Event->framebuffer_height);
                    UpdateMouseScale(g_windowWidth, g_windowHeight, g_windowWidth, g_windowHeight);
                }
                break;
            case SAPP_EVENTTYPE_FOCUSED:
                g_windowFocused = true;
                break;
            case SAPP_EVENTTYPE_UNFOCUSED:
                g_windowFocused = false;
                std::memset(g_mouseDown, 0, sizeof(g_mouseDown));
                std::memset(g_mousePressedPending, 0, sizeof(g_mousePressedPending));
                std::memset(g_mouseReleasedPending, 0, sizeof(g_mouseReleasedPending));
                break;
            case SAPP_EVENTTYPE_MOUSE_ENTER:
                g_mouseInClientArea = true;
                break;
            case SAPP_EVENTTYPE_MOUSE_LEAVE:
                g_mouseInClientArea = false;
                std::memset(g_mouseDown, 0, sizeof(g_mouseDown));
                std::memset(g_mousePressedPending, 0, sizeof(g_mousePressedPending));
                std::memset(g_mouseReleasedPending, 0, sizeof(g_mouseReleasedPending));
                break;
            case SAPP_EVENTTYPE_MOUSE_DOWN:
                if (Event->mouse_button >= 0 && Event->mouse_button < 3)
                {
                    g_mouseDown[Event->mouse_button] = true;
                    g_mousePressedPending[Event->mouse_button] = true;
                }
                g_mouseX = MouseXToFramebuffer(Event->mouse_x);
                g_mouseY = MouseYToFramebuffer(Event->mouse_y);
                break;
            case SAPP_EVENTTYPE_MOUSE_UP:
                if (Event->mouse_button >= 0 && Event->mouse_button < 3)
                {
                    g_mouseDown[Event->mouse_button] = false;
                    g_mouseReleasedPending[Event->mouse_button] = true;
                }
                g_mouseX = MouseXToFramebuffer(Event->mouse_x);
                g_mouseY = MouseYToFramebuffer(Event->mouse_y);
                break;
            case SAPP_EVENTTYPE_MOUSE_MOVE:
                g_mouseX = MouseXToFramebuffer(Event->mouse_x);
                g_mouseY = MouseYToFramebuffer(Event->mouse_y);
                g_mouseDX += Event->mouse_dx * g_mouseScaleX;
                g_mouseDY += Event->mouse_dy * g_mouseScaleY;
                break;
            case SAPP_EVENTTYPE_MOUSE_SCROLL:
                g_mouseScroll += Event->scroll_y;
                break;
            case SAPP_EVENTTYPE_KEY_DOWN:
                if (Event->key_code >= 0 && Event->key_code < kKeyCount)
                {
                    g_keys[Event->key_code] = 1;
                    g_keysPressedPending[Event->key_code] = true;
                }
                break;
            case SAPP_EVENTTYPE_KEY_UP:
                if (Event->key_code >= 0 && Event->key_code < kKeyCount)
                {
                    g_keys[Event->key_code] = 0;
                    g_keysReleasedPending[Event->key_code] = true;
                }
                break;
            default:
                break;
        }
    }

    bool Initialize()
    {
        if (g_initialized) return true;
        std::memset(g_keys, 0, sizeof(g_keys));
        std::memset(g_keysPressed, 0, sizeof(g_keysPressed));
        std::memset(g_keysReleased, 0, sizeof(g_keysReleased));
        std::memset(g_keysPressedPending, 0, sizeof(g_keysPressedPending));
        std::memset(g_keysReleasedPending, 0, sizeof(g_keysReleasedPending));
        std::memset(g_mouseDown, 0, sizeof(g_mouseDown));
        std::memset(g_mousePressed, 0, sizeof(g_mousePressed));
        std::memset(g_mouseReleased, 0, sizeof(g_mouseReleased));
        std::memset(g_mousePressedPending, 0, sizeof(g_mousePressedPending));
        std::memset(g_mouseReleasedPending, 0, sizeof(g_mouseReleasedPending));
        g_mouseX = -1.0f;
        g_mouseY = -1.0f;
        g_mouseDX = 0.0f;
        g_mouseDY = 0.0f;
        g_mouseScroll = 0.0f;
        g_windowFocused = true;
        g_mouseInClientArea = true;
        g_windowWidth = std::max(1, sapp_width());
        g_windowHeight = std::max(1, sapp_height());
        g_mouseScaleX = sapp_dpi_scale();
        g_mouseScaleY = sapp_dpi_scale();
        g_lastFrameTime = 0.0;
        g_initialized = true;
        return true;
    }

    void Shutdown()
    {
        g_initialized = false;
    }

    void RequestQuit()
    {
        sapp_request_quit();
    }

    void ProcessInput()
    {
        for (int i = 0; i < kKeyCount; ++i)
        {
            g_keysPressed[i] = g_keysPressedPending[i];
            g_keysReleased[i] = g_keysReleasedPending[i];
            g_keysPressedPending[i] = false;
            g_keysReleasedPending[i] = false;
        }
        for (int i = 0; i < 3; ++i)
        {
            g_mousePressed[i] = g_mousePressedPending[i];
            g_mouseReleased[i] = g_mouseReleasedPending[i];
            g_mousePressedPending[i] = false;
            g_mouseReleasedPending[i] = false;
        }

        // Frame timing
        // the platform backend gives us sapp_frame_duration() from main; we set g_deltaTime explicitly
        // from frame_cb before calling ProcessInput. Otherwise default 16ms.
        g_lastFrameTime += static_cast<double>(g_deltaTime);
        g_currentTime = g_lastFrameTime;
    }

    bool QuitRequested() { return g_quitRequested; }

    int GetWindowWidth() { return g_windowWidth; }
    int GetWindowHeight() { return g_windowHeight; }
    float GetAspect()
    {
        return g_windowHeight > 0
            ? static_cast<float>(g_windowWidth) / static_cast<float>(g_windowHeight)
            : 1.0f;
    }
    bool IsWindowFocused() { return g_windowFocused; }
    bool IsMouseInClientArea() { return g_mouseInClientArea; }

    bool IsMouseButtonDown(int Button)
    {
        if (Button < 0 || Button > 2) return false;
        if (!g_windowFocused || !g_mouseInClientArea) return false;
        return g_mouseDown[Button];
    }

    bool IsMouseButtonPressed(int Button)
    {
        if (Button < 0 || Button > 2) return false;
        return g_mousePressed[Button];
    }

    bool IsMouseButtonReleased(int Button)
    {
        if (Button < 0 || Button > 2) return false;
        return g_mouseReleased[Button];
    }

    float GetMouseX() { return g_mouseX; }
    float GetMouseY() { return g_mouseY; }
    float GetMouseDeltaX() { float V = g_mouseDX; g_mouseDX = 0.0f; return V; }
    float GetMouseDeltaY() { float V = g_mouseDY; g_mouseDY = 0.0f; return V; }
    float GetMouseWheel()
    {
        float v = g_mouseScroll;
        g_mouseScroll = 0.0f;
        return v;
    }

    void SetMouseCursorVisible(bool Visible)
    {
        g_cursorVisible = Visible;
        sapp_show_mouse(Visible);
    }

    void SetMouseLocked(bool Locked)
    {
        g_cursorLocked = Locked;
        sapp_lock_mouse(Locked);
    }

    bool IsKeyDown(int KeyCode)
    {
        if (KeyCode < 0 || KeyCode >= kKeyCount) return false;
        return g_keys[KeyCode] != 0;
    }

    bool IsKeyPressed(int KeyCode)
    {
        if (KeyCode < 0 || KeyCode >= kKeyCount) return false;
        return g_keysPressed[KeyCode] != 0;
    }

    bool IsKeyReleased(int KeyCode)
    {
        if (KeyCode < 0 || KeyCode >= kKeyCount) return false;
        return g_keysReleased[KeyCode] != 0;
    }

    float GetDeltaTime() { return g_deltaTime; }
    double GetAbsoluteTime() { return g_currentTime; }

    void SetDeltaTime(float Dt)
    {
        if (Dt <= 0.0f) Dt = 0.016f;
        if (Dt > 0.1f) Dt = 0.1f;
        g_deltaTime = Dt;
    }
}
