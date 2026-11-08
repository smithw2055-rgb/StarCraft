#pragma once

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <utility>

#include "Core/Color.h"
#include "Core/Platform/Platform.h"
#include "Render/Draw2D.h"

#ifdef DrawText
#undef DrawText
#endif

namespace Game::Client::GameLoop
{

struct GameMenuRuntime
{
    void ToggleOpenAndPauseWhenOpened()
    {
        Open = !Open;
        if (Open)
            Paused = true;
    }

    void OpenAndPause()
    {
        Open = true;
        Paused = true;
    }

    void EnsureOpenWhenPaused()
    {
        if (Paused)
            Open = true;
    }

    void CloseAndResume()
    {
        Paused = false;
        Open = false;
        StatusText.clear();
    }

    void TogglePause()
    {
        Paused = !Paused;
        StatusText = Paused ? "Game paused" : "Game resumed";
    }

    void SetResult(bool Ok, const char* SuccessText, const char* FailureText)
    {
        HasSaveResult = true;
        LastSaveOk = Ok;
        StatusText = Ok ? SuccessText : FailureText;
    }

    void RestorePaused(bool PausedValue)
    {
        Paused = PausedValue;
    }

    bool IsOpen() const
    {
        return Open;
    }

    bool IsPaused() const
    {
        return Paused;
    }

    bool HasResult() const
    {
        return HasSaveResult;
    }

    bool WasLastSaveOk() const
    {
        return LastSaveOk;
    }

    const std::string& GetStatusText() const
    {
        return StatusText;
    }

    template <typename SaveGameT, typename LoadGameT>
    bool HandleMenuButtonAction(int Button,
                                const std::string& QuickSavePath,
                                SaveGameT&& SaveGame,
                                LoadGameT&& LoadGame)
    {
        switch (Button)
        {
        case 0:
            CloseAndResume();
            return true;
        case 1:
            TogglePause();
            return true;
        case 2:
            SetResult(SaveGame(QuickSavePath), "Saved quicksave.scsave", "Save failed");
            return true;
        case 3:
        {
            const bool Loaded = LoadGame(QuickSavePath);
            SetResult(Loaded, "Loaded quicksave.scsave", "Load failed");
            return true;
        }
        case 4:
            CloseAndResume();
            return true;
        default:
            return false;
        }
    }

    bool Open = false;
    bool Paused = false;
    bool LastSaveOk = false;
    bool HasSaveResult = false;
    std::string StatusText;
};

struct ScreenRectI
{
    int X = 0;
    int Y = 0;
    int W = 0;
    int H = 0;

    bool Contains(int Px, int Py) const
    {
        return Px >= X && Px < X + W && Py >= Y && Py < Y + H;
    }
};

struct GameMenuPresentationState
{
    bool GamePaused = false;
    bool HasSaveResult = false;
    bool LastSaveOk = false;
    std::string StatusText;
};

inline ScreenRectI ResolveHudMenuButtonRect(int ViewportWidth, int ViewportHeight);
inline void RenderHudMenuButton(int ViewportWidth, int ViewportHeight, bool IsOpen);
inline void RenderPauseBanner(int ViewportWidth);
inline int HitGameMenuButton(int ScreenX, int ScreenY, int ViewportWidth, int ViewportHeight);
inline void RenderGameMenuOverlay(
    int ViewportWidth,
    int ViewportHeight,
    int MouseX,
    int MouseY,
    const GameMenuPresentationState& State);

inline void RenderGameMenuPresentationLayer(
    int ViewportWidth,
    int ViewportHeight,
    int MouseX,
    int MouseY,
    const GameMenuRuntime& Menu,
    bool ScenarioPaused)
{
    if ((Menu.IsPaused() || ScenarioPaused) && !Menu.IsOpen())
        RenderPauseBanner(ViewportWidth);

    if (Menu.IsOpen())
    {
        RenderGameMenuOverlay(
            ViewportWidth,
            ViewportHeight,
            MouseX,
            MouseY,
            { Menu.IsPaused(), Menu.HasResult(), Menu.WasLastSaveOk(), Menu.GetStatusText() });
    }
}

template <typename SaveGameT, typename LoadGameT>
inline bool HandleGameMenuInputAction(
    GameMenuRuntime& Menu,
    int ScreenX,
    int ScreenY,
    int ViewportWidth,
    int ViewportHeight,
    const std::string& QuickSavePath,
    SaveGameT&& SaveGame,
    LoadGameT&& LoadGame)
{
    const int Button = HitGameMenuButton(ScreenX, ScreenY, ViewportWidth, ViewportHeight);
    if (Button < 0)
        return false;

    return Menu.HandleMenuButtonAction(
        Button,
        QuickSavePath,
        std::forward<SaveGameT>(SaveGame),
        std::forward<LoadGameT>(LoadGame));
}

template <typename SaveGameT, typename LoadGameT, typename UpdateUiT>
inline bool HandleGameMenuPauseUpdateGate(
    GameMenuRuntime& Menu,
    int MouseX,
    int MouseY,
    int ViewportWidth,
    int ViewportHeight,
    const std::string& QuickSavePath,
    SaveGameT&& SaveGame,
    LoadGameT&& LoadGame,
    UpdateUiT&& UpdateUi)
{
    Menu.EnsureOpenWhenPaused();

    if (Menu.IsOpen())
    {
        if (Core::Platform::IsMouseButtonPressed(0))
        {
            HandleGameMenuInputAction(
                Menu,
                MouseX,
                MouseY,
                ViewportWidth,
                ViewportHeight,
                QuickSavePath,
                std::forward<SaveGameT>(SaveGame),
                std::forward<LoadGameT>(LoadGame));
        }
        UpdateUi();
        return true;
    }

    if (Menu.IsPaused())
    {
        UpdateUi();
        return true;
    }

    return false;
}

inline ScreenRectI ResolveHudMenuButtonRect(int ViewportWidth, int ViewportHeight)
{
    constexpr int FhdLogicalWidth = 1920;
    constexpr int FhdLogicalHeight = 1080;
    constexpr int FhdConsoleHeight = 320;
    constexpr int TerranConsoleRightSourceWidth = 2286;
    constexpr int TerranConsoleRightSourceHeight = 700;
    constexpr int TerranConsolePortraitSourceWidth = 256;
    constexpr int PortraitAnchorX = 1318 - 380;
    constexpr int PortraitAnchorY = 373 + 60;

    const float CanvasScale = std::max(0.01f, std::min((float)ViewportWidth / (float)FhdLogicalWidth,
                                                        (float)ViewportHeight / (float)FhdLogicalHeight));
    const int HudHeight = std::max(1, (int)std::lround((float)FhdConsoleHeight * CanvasScale));
    const int ConsoleY = ViewportHeight - HudHeight;
    const float BaseScale = (float)HudHeight / (float)TerranConsoleRightSourceHeight;
    const int RightBaseW = std::max(1, (int)std::lround((float)TerranConsoleRightSourceWidth * BaseScale));
    const int RightBaseX = ViewportWidth - RightBaseW;
    const int PortraitX = RightBaseX + (int)std::lround((float)PortraitAnchorX * BaseScale);
    const int PortraitY = ConsoleY + (int)std::lround((float)PortraitAnchorY * BaseScale);
    const int PortraitW = std::max(1, (int)std::lround((float)TerranConsolePortraitSourceWidth * BaseScale));

    const int ButtonW = std::clamp((int)std::lround((float)PortraitW * 0.72f), 54, 78);
    const int ButtonH = std::clamp((int)std::lround(24.0f * CanvasScale / (2.0f / 3.0f)), 20, 28);
    return
    {
        PortraitX + (PortraitW - ButtonW) / 2,
        std::max(ConsoleY + 4, PortraitY - ButtonH - 6),
        ButtonW,
        ButtonH
    };
}

inline void RenderHudMenuButton(int ViewportWidth, int ViewportHeight, bool IsOpen)
{
    const ScreenRectI HudMenuRect = ResolveHudMenuButtonRect(ViewportWidth, ViewportHeight);
    Render::g_Draw2D.DrawRect((float)HudMenuRect.X, (float)HudMenuRect.Y,
                              (float)HudMenuRect.W, (float)HudMenuRect.H,
                              IsOpen ? Render::Color(22, 48, 72, 230)
                                     : Render::Color(8, 16, 26, 190));
    Render::g_Draw2D.DrawRectLines((float)HudMenuRect.X, (float)HudMenuRect.Y,
                                   (float)HudMenuRect.W, (float)HudMenuRect.H,
                                   Render::Color(112, 160, 190, 210), 1.0f);
    Render::g_Draw2D.DrawText("MENU", (float)HudMenuRect.X + 9.0f,
                              (float)HudMenuRect.Y + 7.0f, 10.0f,
                              Render::Color(230, 240, 248, 255));
}

inline void RenderPauseBanner(int ViewportWidth)
{
    Render::g_Draw2D.DrawRect((float)(ViewportWidth / 2 - 70), 42.0f, 140.0f, 28.0f,
                              Render::Color(0, 0, 0, 150));
    Render::g_Draw2D.DrawText("PAUSED", (float)(ViewportWidth / 2 - 38), 50.0f, 13.0f,
                              Render::Color(245, 238, 190, 255));
}

inline int HitGameMenuButton(int ScreenX, int ScreenY, int ViewportWidth, int ViewportHeight)
{
    const int PanelW = 320;
    const int PanelH = 292;
    const int PanelX = (ViewportWidth - PanelW) / 2;
    const int PanelY = (ViewportHeight - PanelH) / 2;
    const int ButtonW = 220;
    const int ButtonH = 34;
    const int ButtonX = PanelX + (PanelW - ButtonW) / 2;
    const int FirstY = PanelY + 70;
    const int Gap = 12;

    for (int I = 0; I < 5; ++I)
    {
        const int Y = FirstY + I * (ButtonH + Gap);
        if (ScreenX >= ButtonX && ScreenX < ButtonX + ButtonW &&
            ScreenY >= Y && ScreenY < Y + ButtonH)
        {
            return I;
        }
    }
    return -1;
}

inline void RenderGameMenuOverlay(
    int ViewportWidth,
    int ViewportHeight,
    int MouseX,
    int MouseY,
    const GameMenuPresentationState& State)
{
    auto& D = Render::g_Draw2D;
    const int PanelW = 320;
    const int PanelH = 292;
    const int PanelX = (ViewportWidth - PanelW) / 2;
    const int PanelY = (ViewportHeight - PanelH) / 2;
    const int ButtonW = 220;
    const int ButtonH = 34;
    const int ButtonX = PanelX + (PanelW - ButtonW) / 2;
    const int FirstY = PanelY + 70;
    const int Gap = 12;

    D.DrawRect(0.0f, 0.0f, (float)ViewportWidth, (float)ViewportHeight, Render::Color(0, 0, 0, 105));
    D.DrawRect((float)PanelX, (float)PanelY, (float)PanelW, (float)PanelH, Render::Color(8, 14, 22, 238));
    D.DrawRectLines((float)PanelX, (float)PanelY, (float)PanelW, (float)PanelH,
                    Render::Color(92, 132, 160, 230), 1.0f);
    D.DrawRect((float)PanelX + 2.0f, (float)PanelY + 2.0f, (float)PanelW - 4.0f, 36.0f,
               Render::Color(18, 42, 64, 210));
    D.DrawText("GAME MENU", (float)PanelX + 98.0f, (float)PanelY + 12.0f, 14.0f,
               Render::Color(236, 242, 250, 255));

    const char* Labels[5] =
    {
        "Continue",
        State.GamePaused ? "Resume Game" : "Pause Game",
        "Save Game",
        "Load Game",
        "Close Menu"
    };

    for (int I = 0; I < 5; ++I)
    {
        const int Y = FirstY + I * (ButtonH + Gap);
        const bool Hovered = MouseX >= ButtonX && MouseX < ButtonX + ButtonW && MouseY >= Y && MouseY < Y + ButtonH;
        D.DrawRect((float)ButtonX, (float)Y, (float)ButtonW, (float)ButtonH,
                   Hovered ? Render::Color(35, 78, 104, 235) : Render::Color(20, 34, 48, 225));
        D.DrawRectLines((float)ButtonX, (float)Y, (float)ButtonW, (float)ButtonH,
                        Hovered ? Render::Color(155, 210, 232, 240) : Render::Color(88, 118, 140, 210),
                        1.0f);
        D.DrawText(Labels[I], (float)ButtonX + 18.0f, (float)Y + 10.0f, 12.0f,
                   Render::Color(232, 238, 244, 255));
    }

    const std::string Status = State.StatusText.empty()
        ? (State.GamePaused ? "Paused" : "Running")
        : State.StatusText;
    D.DrawText(Status, (float)PanelX + 20.0f, (float)PanelY + (float)PanelH - 24.0f, 11.0f,
               (State.HasSaveResult && !State.LastSaveOk)
                   ? Render::Color(255, 130, 120, 255)
                   : Render::Color(190, 218, 226, 255));
}

} // namespace Game::Client::GameLoop
