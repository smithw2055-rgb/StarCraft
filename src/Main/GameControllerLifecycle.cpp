#include "Main/GameController.h"

#include "Core/Platform/Platform.h"
#include "Core/Utils/InputDebug.h"
#include "Main/GameClientLifecycle.h"
#include "Main/GameGlueMenuActionRuntime.h"
#include "Main/GameGuiBootstrap.h"

#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <sstream>
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

    void LogControllerViewportState(const char* Stage)
    {
        if (!Core::Utils::InputDebugEnabled())
            return;

        std::ostringstream Stream;
        Stream << "stage=" << Stage
               << " logical=" << Core::Platform::GetWindowWidth() << "x" << Core::Platform::GetWindowHeight()
               << " client=" << Core::Platform::GetClientWidth() << "x" << Core::Platform::GetClientHeight()
               << " framebuffer=" << Core::Platform::GetFramebufferWidth() << "x" << Core::Platform::GetFramebufferHeight();
        Core::Utils::InputDebugLog("InputControllerState", Stream.str());
    }
}

bool GameController::Initialize(int Width, int Height)
{
    (void)Width;
    (void)Height;
    std::cout << "StarCraft: init" << std::endl;
    std::cout << "GameController.Initialize this=" << (uintptr_t)this
              << " runtime=" << (uintptr_t)&mRuntime
              << " client_runtime=" << (uintptr_t)&mRuntime.Client.Runtime
              << std::endl;

    if (!InitializeClientShellRuntime())
        return false;

    LogControllerViewportState("post_shell_init");

    mRuntime.Client.Runtime.InitializeWorldEffects(800);
    mRuntime.App.Audio.Initialize();

    LogControllerViewportState("pre_gui_init");
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

    LogControllerViewportState("post_gui_init");

    mRuntime.App.Runtime.Initialized = true;
    mRuntime.App.Runtime.State = GameState::MainMenu;
    if (ShouldDevStartInGame(mRuntime.App.Runtime, mDirectStartEnabled))
    {
        LogControllerViewportState("pre_start_game");
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
