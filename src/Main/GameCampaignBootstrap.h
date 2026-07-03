#pragma once

#include <string>

#include "Client/ClientRuntime.h"
#include "Main/CampaignRuntime.h"
#include "Main/GameAudioRuntime.h"
#include "Main/GameSession.h"

namespace Game::Client::GameLoop
{

inline void InitializeCampaignRuntimeForSession(
    CampaignRuntime& Campaign,
    GameSession& Session,
    GameAudioRuntime& Audio,
    ClientRuntime& Client,
    const std::string& MapPath)
{
    auto& Entities = Session.Systems().GetEntityManager();
    Campaign.InitializeCampaign(
        Entities,
        Session.GetScriptData().Triggers.empty() ? nullptr : &Session.GetScriptData());

    if (!Campaign.IsActive() || !Campaign.HasTriggerRuntime())
        return;

    Audio.ConfigureCampaignMapAudio(
        Campaign,
        MapPath,
        [&Client]() { return Client.GetCameraPosition(); });

    Campaign.ConfigureTriggerRuntime(
        [&Session](int PlayerA, int PlayerB, bool Allied) {
            if (auto* FogSystem = Session.GetFogSystem())
                FogSystem->SetAlliance(PlayerA, PlayerB, Allied);
        },
        [&Client](int ChkUnitID, auto, int) {
            return (int)Client.GetSpriteAssets().ResolveImageID(ChkUnitID);
        },
        [&Session, &Client](auto Position) {
            if (!Session.IsMapLoaded())
                return;
            Client.CenterCamera(Position.X, Position.Y);
        },
        [&Client](auto Position) {
            Client.AddCampaignPing(Position);
        },
        [&Campaign](bool Paused) {
            Campaign.SetScenarioPaused(Paused);
        });
}

} // namespace Game::Client::GameLoop
