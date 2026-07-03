#pragma once

#include <string>
#include <utility>

#include "Core/Platform/Platform.h"
#include "Main/GameMenuRuntime.h"

namespace Game::Client::GameLoop
{

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

ScreenRectI ResolveHudMenuButtonRect(int ViewportWidth, int ViewportHeight);
void RenderHudMenuButton(int ViewportWidth, int ViewportHeight, bool IsOpen);
void RenderPauseBanner(int ViewportWidth);
int HitGameMenuButton(int ScreenX, int ScreenY, int ViewportWidth, int ViewportHeight);
void RenderGameMenuOverlay(
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

} // namespace Game::Client::GameLoop
