#include "Main/GameController.h"

#include "Core/Platform/Platform.h"
#include "Main/GameClientLifecycle.h"
#include "Main/GameGlueMenuActionRuntime.h"
#include "Main/GameGuiBootstrap.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace Game::Client::GameLoop
{

namespace
{
    bool ShouldDevStartInGame(const AppRuntime& App, bool DirectStartEnabled)
    {
        if (DirectStartEnabled)
            return true;
        if (!App.MapPathOverride.empty())
            return true;
        const char* DevStart = std::getenv("STARCRAFT_DEV_START_INGAME");
        return DevStart && std::string(DevStart) == "1";
    }
}

bool GameController::Initialize(int Width, int Height)
{
    (void)Width;
    (void)Height;
    std::cout << "StarCraft: init" << std::endl;

    if (!InitializeClientShellRuntime())
        return false;

    mRuntime.Client.Runtime.InitializeWorldEffects(800);
    mRuntime.App.Audio.Initialize();

    InitializeGuiRuntime(
        mRuntime.App.Gui,
        mRuntime.App.Runtime.AssetRoot,
        Core::Platform::GetWindowWidth(),
        Core::Platform::GetWindowHeight(),
        [this](const std::string& Action) {
        HandleGlueMenuAction(
            Action,
            mRuntime.Session.Campaign,
            []() { Core::Platform::RequestQuit(); },
            [this](const std::string& MapPath) { SetMapPathOverride(MapPath); },
            [this]() { StartGame(); });
    },
        !mDirectStartEnabled);
#if ENABLE_GUI_UI
    if (mInitialGuiHudEnabled && mRuntime.App.Gui.MainGui.IsActive())
        mRuntime.App.Gui.UseGui = true;
#endif

    mRuntime.App.Runtime.Initialized = true;
    mRuntime.App.Runtime.State = GameState::MainMenu;
    if (ShouldDevStartInGame(mRuntime.App.Runtime, mDirectStartEnabled))
    {
        if (mDirectStartEnabled)
            std::cout << "StarCraft: direct test map startup entering game" << std::endl;
        StartGame();
    }
    return true;
}

void GameController::Shutdown()
{
    std::cout << "StarCraft: cleanup" << std::endl;
    ShutdownGuiRuntime(mRuntime.App.Gui);
    ShutdownClientShellRuntime(mRuntime.Client.Runtime, mRuntime.Client.Hud);
}

} // namespace Game::Client::GameLoop
