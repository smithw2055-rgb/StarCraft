#include "Main/GameController.h"

#include "Core/Platform/Platform.h"
#include "Main/GameClientLifecycle.h"
#include "Main/GameGlueMenuActionRuntime.h"
#include "Main/GameGuiBootstrap.h"

#include <iostream>
#include <string>

namespace Game::Client::GameLoop
{

namespace
{
    constexpr bool kDevStartInGame = true;
}

bool GameController::Initialize(int Width, int Height)
{
    std::cout << "StarCraft: init" << std::endl;

    if (!InitializeClientShellRuntime())
        return false;

    mRuntime.Client.Runtime.InitializeWorldEffects(800);
    mRuntime.App.Audio.Initialize();

    InitializeGuiRuntime(mRuntime.App.Gui, mRuntime.App.Runtime.AssetRoot, Width, Height, [this](const std::string& Action) {
        HandleGlueMenuAction(
            Action,
            mRuntime.Session.Campaign,
            []() { Core::Platform::RequestQuit(); },
            [this](const std::string& MapPath) { SetMapPathOverride(MapPath); },
            [this]() { StartGame(); });
    });

    mRuntime.App.Runtime.Initialized = true;
    mRuntime.App.Runtime.State = GameState::MainMenu;
    if (kDevStartInGame)
        StartGame();
    return true;
}

void GameController::Shutdown()
{
    std::cout << "StarCraft: cleanup" << std::endl;
    ShutdownGuiRuntime(mRuntime.App.Gui);
    ShutdownClientShellRuntime(mRuntime.Client.Runtime, mRuntime.Client.Hud);
}

} // namespace Game::Client::GameLoop
