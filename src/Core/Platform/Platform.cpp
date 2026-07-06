#include "Core/Platform/Platform.h"
#include "Core/Platform/MouseCoordinate.h"
#include "Core/Utils/InputDebug.h"

#include "third_party/sokol/sokol_app.h"

#include <algorithm>
#include <cstring>
#include <cmath>
#include <sstream>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace
{
    bool g_initialized = false;
    bool g_quitRequested = false;

    int g_windowWidth = 1280;
    int g_windowHeight = 720;
    int g_framebufferWidth = 1280;
    int g_framebufferHeight = 720;
    bool g_windowFocused = true;
    bool g_mouseInClientArea = true;
    bool g_mouseInGameContentArea = false;

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
    float g_mouseOffsetX = 0.0f;
    float g_mouseOffsetY = 0.0f;
    float g_mouseMaxX = 0.0f;
    float g_mouseMaxY = 0.0f;
    int g_mouseSourceWidth = 1280;
    int g_mouseSourceHeight = 720;
    float g_nativeWindowScale = 1.0f;

    static constexpr int kKeyCount = 512;
    uint8_t g_keys[kKeyCount] = {};
    bool g_keysPressed[kKeyCount] = {};
    bool g_keysReleased[kKeyCount] = {};
    bool g_keysPressedPending[kKeyCount] = {};
    bool g_keysReleasedPending[kKeyCount] = {};

    double g_lastFrameTime = 0.0;
    double g_currentTime = 0.0;
    float g_deltaTime = 0.016f;

    void SyncNativeMouseSource()
    {
#if defined(_WIN32)
        g_mouseSourceWidth = g_framebufferWidth;
        g_mouseSourceHeight = g_framebufferHeight;
        g_nativeWindowScale = sapp_dpi_scale();

        if (const void* HwndPtr = sapp_win32_get_hwnd())
        {
            const HWND Hwnd = static_cast<HWND>(const_cast<void*>(HwndPtr));
            RECT Rect = {};
            if (GetClientRect(Hwnd, &Rect))
            {
                const int ClientWidth = std::max(1, (int)(Rect.right - Rect.left));
                const int ClientHeight = std::max(1, (int)(Rect.bottom - Rect.top));
                g_mouseSourceWidth = ClientWidth;
                g_mouseSourceHeight = ClientHeight;
            }

            using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
            if (HMODULE User32 = GetModuleHandleW(L"user32.dll"))
            {
                const auto Fn =
                    reinterpret_cast<GetDpiForWindowFn>(GetProcAddress(User32, "GetDpiForWindow"));
                if (Fn)
                    g_nativeWindowScale = (float)Fn(Hwnd) / 96.0f;
            }
        }
#else
        g_mouseSourceWidth = g_framebufferWidth;
        g_mouseSourceHeight = g_framebufferHeight;
        g_nativeWindowScale = sapp_dpi_scale();
#endif
    }

    bool QueryNativeCursorPosition(float& OutX, float& OutY, bool& OutInside)
    {
#if defined(_WIN32)
        if (const void* HwndPtr = sapp_win32_get_hwnd())
        {
            const HWND Hwnd = static_cast<HWND>(const_cast<void*>(HwndPtr));
            POINT Point = {};
            if (GetCursorPos(&Point) && ScreenToClient(Hwnd, &Point))
            {
                RECT Rect = {};
                if (GetClientRect(Hwnd, &Rect))
                {
                    OutX = static_cast<float>(Point.x);
                    OutY = static_cast<float>(Point.y);
                    OutInside =
                        Point.x >= Rect.left && Point.x < Rect.right &&
                        Point.y >= Rect.top && Point.y < Rect.bottom;
                    return true;
                }
            }
        }
#endif
        (void)OutX;
        (void)OutY;
        (void)OutInside;
        return false;
    }

    void RefreshWindowMetricsFromNativeClient()
    {
#if defined(_WIN32)
        SyncNativeMouseSource();
#endif
    }

    void UpdateMouseScale(int WindowWidth, int WindowHeight, int MouseSourceWidth, int MouseSourceHeight)
    {
        const Core::Platform::MouseScale Scale =
            Core::Platform::ComputeMouseScale(
                WindowWidth, WindowHeight,
                MouseSourceWidth, MouseSourceHeight,
                sapp_dpi_scale());
        g_mouseScaleX = Scale.X;
        g_mouseScaleY = Scale.Y;
        g_mouseOffsetX = Scale.OffsetX;
        g_mouseOffsetY = Scale.OffsetY;
        g_mouseMaxX = Scale.MaxX;
        g_mouseMaxY = Scale.MaxY;
    }

    void LogPlatformState(const char* Stage)
    {
        if (!Core::Utils::InputDebugEnabled())
            return;

        std::ostringstream Stream;
        Stream << "stage=" << Stage
               << " logical=" << g_windowWidth << "x" << g_windowHeight
               << " client=" << g_mouseSourceWidth << "x" << g_mouseSourceHeight
               << " framebuffer=" << g_framebufferWidth << "x" << g_framebufferHeight
               << " sapp_dpi=" << sapp_dpi_scale()
               << " native_scale=" << g_nativeWindowScale
               << " scale=" << g_mouseScaleX << "," << g_mouseScaleY
               << " offset=" << g_mouseOffsetX << "," << g_mouseOffsetY
               << " max=" << g_mouseMaxX << "," << g_mouseMaxY;
        Core::Utils::InputDebugLog("InputPlatformState", Stream.str());
    }

    void SyncDimensionsFromEvent(const sapp_event& Event)
    {
        const int EventWindowWidth = Event.window_width > 0 ? Event.window_width : g_windowWidth;
        const int EventWindowHeight = Event.window_height > 0 ? Event.window_height : g_windowHeight;
        const int EventFramebufferWidth = Event.framebuffer_width > 0 ? Event.framebuffer_width : g_framebufferWidth;
        const int EventFramebufferHeight = Event.framebuffer_height > 0 ? Event.framebuffer_height : g_framebufferHeight;

        if (EventWindowWidth <= 0 || EventWindowHeight <= 0 ||
            EventFramebufferWidth <= 0 || EventFramebufferHeight <= 0)
        {
            return;
        }

        const int PrevWindowWidth = g_windowWidth;
        const int PrevWindowHeight = g_windowHeight;
        const int PrevFramebufferWidth = g_framebufferWidth;
        const int PrevFramebufferHeight = g_framebufferHeight;
        g_framebufferWidth = EventFramebufferWidth;
        g_framebufferHeight = EventFramebufferHeight;
#if defined(_WIN32)
        RefreshWindowMetricsFromNativeClient();
        const float DpiScale =
            std::max(0.0001f, std::max(g_nativeWindowScale, sapp_dpi_scale()));
        if (g_mouseSourceWidth > 0 && g_mouseSourceHeight > 0)
        {
            g_windowWidth = std::max(1, (int)std::lround((float)g_mouseSourceWidth / DpiScale));
            g_windowHeight = std::max(1, (int)std::lround((float)g_mouseSourceHeight / DpiScale));
        }
        else
        {
            g_windowWidth = EventWindowWidth;
            g_windowHeight = EventWindowHeight;
        }
#else
        g_windowWidth = EventWindowWidth;
        g_windowHeight = EventWindowHeight;
        SyncNativeMouseSource();
#endif

        if (PrevWindowWidth == g_windowWidth &&
            PrevWindowHeight == g_windowHeight &&
            PrevFramebufferWidth == g_framebufferWidth &&
            PrevFramebufferHeight == g_framebufferHeight)
        {
            return;
        }

        UpdateMouseScale(g_windowWidth, g_windowHeight, g_mouseSourceWidth, g_mouseSourceHeight);
        LogPlatformState("sync_event");
    }

    const char* EventTypeName(sapp_event_type Type)
    {
        switch (Type)
        {
            case SAPP_EVENTTYPE_MOUSE_DOWN: return "mouse_down";
            case SAPP_EVENTTYPE_MOUSE_UP: return "mouse_up";
            case SAPP_EVENTTYPE_MOUSE_MOVE: return "mouse_move";
            case SAPP_EVENTTYPE_MOUSE_SCROLL: return "mouse_scroll";
            case SAPP_EVENTTYPE_RESIZED: return "resized";
            default: return "other";
        }
    }

    void LogResizeEvent(const sapp_event& Event)
    {
        if (!Core::Utils::InputDebugEnabled())
            return;

        std::ostringstream Stream;
        Stream << "type=resized"
               << " event_window=" << Event.window_width << "x" << Event.window_height
               << " event_fb=" << Event.framebuffer_width << "x" << Event.framebuffer_height
               << " dpi=" << sapp_dpi_scale()
               << " logical=" << g_windowWidth << "x" << g_windowHeight
               << " client=" << g_mouseSourceWidth << "x" << g_mouseSourceHeight
               << " framebuffer=" << g_framebufferWidth << "x" << g_framebufferHeight
               << " native_scale=" << g_nativeWindowScale
               << " scale=" << g_mouseScaleX << "," << g_mouseScaleY
               << " offset=" << g_mouseOffsetX << "," << g_mouseOffsetY
               << " max=" << g_mouseMaxX << "," << g_mouseMaxY;
        Core::Utils::InputDebugLog("InputPlatform", Stream.str());
    }

    void LogMouseEvent(const sapp_event& Event, float RawX, float RawY, float MappedX, float MappedY)
    {
        const bool IsMove = Event.type == SAPP_EVENTTYPE_MOUSE_MOVE;
        if (!Core::Utils::InputDebugEnabled())
            return;
        if (IsMove && !Core::Utils::InputDebugShouldLogMove())
            return;

        std::ostringstream Stream;
        Stream << "type=" << EventTypeName(Event.type)
               << " button=" << Event.mouse_button
               << " raw=" << RawX << "," << RawY
               << " mapped=" << MappedX << "," << MappedY
               << " inside_client=" << (g_mouseInClientArea ? 1 : 0)
               << " inside_content=" << (g_mouseInGameContentArea ? 1 : 0)
               << " delta=" << Event.mouse_dx << "," << Event.mouse_dy
               << " logical=" << g_windowWidth << "x" << g_windowHeight
               << " client_size=" << g_mouseSourceWidth << "x" << g_mouseSourceHeight
               << " framebuffer=" << g_framebufferWidth << "x" << g_framebufferHeight
               << " native_scale=" << g_nativeWindowScale
               << " scale=" << g_mouseScaleX << "," << g_mouseScaleY;
        Core::Utils::InputDebugLog("InputPlatform", Stream.str());
    }

    void UpdateMousePosition(float RawX, float RawY)
    {
        float NativeX = RawX;
        float NativeY = RawY;
        bool NativeInside = g_mouseInClientArea;
        if (QueryNativeCursorPosition(NativeX, NativeY, NativeInside))
        {
            const auto MappedX = Core::Platform::MouseCoordToFramebufferChecked(
                NativeX, g_mouseScaleX, g_mouseOffsetX, g_mouseMaxX);
            const auto MappedY = Core::Platform::MouseCoordToFramebufferChecked(
                NativeY, g_mouseScaleY, g_mouseOffsetY, g_mouseMaxY);
            g_mouseX = MappedX.Value;
            g_mouseY = MappedY.Value;
            g_mouseInClientArea = NativeInside;
            g_mouseInGameContentArea =
                NativeInside && MappedX.InsideContent && MappedY.InsideContent;
            return;
        }

        const auto MappedX = Core::Platform::MouseCoordToFramebufferChecked(
            RawX, g_mouseScaleX, g_mouseOffsetX, g_mouseMaxX);
        const auto MappedY = Core::Platform::MouseCoordToFramebufferChecked(
            RawY, g_mouseScaleY, g_mouseOffsetY, g_mouseMaxY);
        g_mouseX = MappedX.Value;
        g_mouseY = MappedY.Value;
        g_mouseInGameContentArea =
            g_mouseInClientArea && MappedX.InsideContent && MappedY.InsideContent;
    }
}

namespace Core::Platform
{
    void HandleEvent(const void* EventPtr)
    {
        const sapp_event* Event = static_cast<const sapp_event*>(EventPtr);
        if (!Event) return;
        SyncDimensionsFromEvent(*Event);
        switch (Event->type)
        {
            case SAPP_EVENTTYPE_QUIT_REQUESTED:
                g_quitRequested = true;
                break;
            case SAPP_EVENTTYPE_RESIZED:
                LogResizeEvent(*Event);
                break;
            case SAPP_EVENTTYPE_FOCUSED:
                g_windowFocused = true;
                break;
            case SAPP_EVENTTYPE_UNFOCUSED:
                g_windowFocused = false;
                g_mouseInGameContentArea = false;
                std::memset(g_mouseDown, 0, sizeof(g_mouseDown));
                std::memset(g_mousePressedPending, 0, sizeof(g_mousePressedPending));
                std::memset(g_mouseReleasedPending, 0, sizeof(g_mouseReleasedPending));
                break;
            case SAPP_EVENTTYPE_MOUSE_ENTER:
                g_mouseInClientArea = true;
                break;
            case SAPP_EVENTTYPE_MOUSE_LEAVE:
                g_mouseInClientArea = false;
                g_mouseInGameContentArea = false;
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
                UpdateMousePosition(Event->mouse_x, Event->mouse_y);
                LogMouseEvent(*Event, Event->mouse_x, Event->mouse_y, g_mouseX, g_mouseY);
                break;
            case SAPP_EVENTTYPE_MOUSE_UP:
                if (Event->mouse_button >= 0 && Event->mouse_button < 3)
                {
                    g_mouseDown[Event->mouse_button] = false;
                    g_mouseReleasedPending[Event->mouse_button] = true;
                }
                UpdateMousePosition(Event->mouse_x, Event->mouse_y);
                LogMouseEvent(*Event, Event->mouse_x, Event->mouse_y, g_mouseX, g_mouseY);
                break;
            case SAPP_EVENTTYPE_MOUSE_MOVE:
                UpdateMousePosition(Event->mouse_x, Event->mouse_y);
                g_mouseDX += Core::Platform::MouseDeltaToFramebuffer(Event->mouse_dx, g_mouseScaleX);
                g_mouseDY += Core::Platform::MouseDeltaToFramebuffer(Event->mouse_dy, g_mouseScaleY);
                LogMouseEvent(*Event, Event->mouse_x, Event->mouse_y, g_mouseX, g_mouseY);
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
        g_mouseInGameContentArea = false;
        g_framebufferWidth = std::max(1, sapp_width());
        g_framebufferHeight = std::max(1, sapp_height());
#if defined(_WIN32)
        RefreshWindowMetricsFromNativeClient();
        const float DpiScale =
            std::max(0.0001f, std::max(g_nativeWindowScale, sapp_dpi_scale()));
        g_windowWidth = std::max(1, (int)std::lround((float)g_mouseSourceWidth / DpiScale));
        g_windowHeight = std::max(1, (int)std::lround((float)g_mouseSourceHeight / DpiScale));
#else
        const float DpiScale = sapp_dpi_scale();
        g_windowWidth = std::max(1, (int)std::lround((float)g_framebufferWidth / std::max(0.0001f, DpiScale)));
        g_windowHeight = std::max(1, (int)std::lround((float)g_framebufferHeight / std::max(0.0001f, DpiScale)));
        SyncNativeMouseSource();
#endif
        UpdateMouseScale(g_windowWidth, g_windowHeight, g_mouseSourceWidth, g_mouseSourceHeight);
        LogPlatformState("initialize");
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

        float NativeX = 0.0f;
        float NativeY = 0.0f;
        bool NativeInside = false;
        if (QueryNativeCursorPosition(NativeX, NativeY, NativeInside))
        {
            const auto MappedX = Core::Platform::MouseCoordToFramebufferChecked(
                NativeX, g_mouseScaleX, g_mouseOffsetX, g_mouseMaxX);
            const auto MappedY = Core::Platform::MouseCoordToFramebufferChecked(
                NativeY, g_mouseScaleY, g_mouseOffsetY, g_mouseMaxY);
            g_mouseX = MappedX.Value;
            g_mouseY = MappedY.Value;
            g_mouseInClientArea = NativeInside;
            g_mouseInGameContentArea =
                NativeInside && MappedX.InsideContent && MappedY.InsideContent;
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
    int GetClientWidth() { return g_mouseSourceWidth; }
    int GetClientHeight() { return g_mouseSourceHeight; }
    int GetFramebufferWidth() { return g_framebufferWidth; }
    int GetFramebufferHeight() { return g_framebufferHeight; }
    float GetAspect()
    {
        return g_windowHeight > 0
            ? static_cast<float>(g_windowWidth) / static_cast<float>(g_windowHeight)
            : 1.0f;
    }
    bool IsWindowFocused() { return g_windowFocused; }
    bool IsMouseInClientArea() { return g_mouseInClientArea; }
    bool IsMouseInGameContentArea() { return g_mouseInGameContentArea; }

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
