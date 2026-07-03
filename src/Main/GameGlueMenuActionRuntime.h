#pragma once

#include <string>
#include <utility>

#include "Game/Campaign/CampaignMissionCatalog.h"
#include "Main/CampaignRuntime.h"

namespace Game::Client::GameLoop
{

template <typename QuitT, typename SetMapPathT, typename StartGameT>
inline void HandleGlueMenuAction(
    const std::string& Action,
    CampaignRuntime& Campaign,
    QuitT&& Quit,
    SetMapPathT&& SetMapPath,
    StartGameT&& StartGame)
{
    if (Action == "exit")
    {
        Quit();
    }
    else if (Game::Campaign::IsCampaignMissionAction(Action))
    {
        const Game::Campaign::CampaignMissionRef Mission = Game::Campaign::ResolveCampaignMission(Action);
        if (Mission.Valid)
        {
            Campaign.SetProgressTarget(Mission.Expansion, Mission.Race, Mission.Mission);
            SetMapPath(Mission.Path);
        }
        StartGame();
    }
    else if (Action == "start_original" || Action == "start_expansion" ||
             Action == "start_custom_game")
    {
        Campaign.ClearProgressTarget();
        StartGame();
    }
}

} // namespace Game::Client::GameLoop
