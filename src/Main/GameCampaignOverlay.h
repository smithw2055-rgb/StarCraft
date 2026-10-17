#pragma once

#include "Client/ClientRuntime.h"
#include "Main/CampaignPresentation.h"
#include "Main/CampaignRuntime.h"
#include "Main/GameSession.h"
#include "UI/Hud.h"

namespace Game::Client::GameLoop
{

inline void RenderActiveCampaignOverlay(CampaignRuntime& Campaign,
                                        UI::Hud& Hud,
                                        Game::Client::ClientRuntime& Client,
                                        int ViewportWidth,
                                        int ViewportHeight)
{
    if (!Campaign.IsActive())
        return;

    RenderCampaignOverlay(
        Campaign.Controller(),
        Hud,
        Client.GetDataAssets(),
        Client.GetArchiveSource(),
        Client.GetUIAssets(),
        ViewportWidth,
        ViewportHeight);
}

inline bool IsAiObserverMatchActive()
{
    const char* Value = std::getenv("STARCRAFT_AI_OBSERVER_MATCH");
    return Value && Value[0] != '\0' && Value[0] != '0';
}

inline void RenderActiveCampaignOverlay(CampaignRuntime& Campaign,
                                        UI::Hud& Hud,
                                        Game::Client::ClientRuntime& Client,
                                        GameSession& Session,
                                        int ViewportWidth,
                                        int ViewportHeight)
{
    if (IsAiObserverMatchActive())
    {
        if (Session.IsMatchOver())
        {
            RenderMatchGameOverOverlay(
                Session.GetMatchOutcome(),
                ViewportWidth,
                ViewportHeight,
                Session.GetMatchElapsedSeconds());
        }
        return;
    }

    if (Campaign.IsActive())
    {
        RenderCampaignOverlay(
            Campaign.Controller(),
            Hud,
            Client.GetDataAssets(),
            Client.GetArchiveSource(),
            Client.GetUIAssets(),
            ViewportWidth,
            ViewportHeight);
    }
    else if (Session.IsMatchOver())
    {
        RenderMatchGameOverOverlay(
            Session.GetMatchOutcome(),
            ViewportWidth,
            ViewportHeight,
            Session.GetMatchElapsedSeconds());
    }
}

} // namespace Game::Client::GameLoop
