#pragma once

#include "Client/ClientRuntime.h"
#include "Main/CampaignPresentation.h"
#include "Main/CampaignRuntime.h"
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

} // namespace Game::Client::GameLoop
