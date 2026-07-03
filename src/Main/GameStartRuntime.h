#pragma once

#include <iostream>

#include "Main/AppRuntime.h"
#include "Main/CampaignRuntime.h"
#include "Main/GameMenuRuntime.h"
#include "Main/GameSession.h"

namespace Game::Client::GameLoop
{

template <typename InitGameSystemsT>
inline void StartGameFromMainMenu(AppRuntime& App,
                                  GameMenuRuntime& Menu,
                                  CampaignRuntime& Campaign,
                                  GameSession& Session,
                                  InitGameSystemsT&& InitGameSystems)
{
    if (App.State != GameState::MainMenu)
        return;

    std::cout << "StarCraft: Starting game..." << std::endl;
    Menu.CloseAndResume();
    Campaign.SetScenarioPaused(false);

    InitGameSystems();
    if (!Session.IsEcsInitialized())
    {
        std::cerr << "Game init failed!" << std::endl;
        return;
    }

    App.State = GameState::InGame;
    std::cout << "StarCraft: Game started" << std::endl;
}

} // namespace Game::Client::GameLoop
