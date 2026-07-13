#pragma once

#include <string>

#include "Client/ClientRuntime.h"
#include "Main/CampaignRuntime.h"
#include "Main/GameAudioRuntime.h"
#include "Main/GameSession.h"
#include "Game/Systems/ZergSystem.h"
#include "Resource/GameDataLoader.h"

namespace Game::Client::GameLoop
{

inline void InitializeCampaignRuntimeForSession(
    CampaignRuntime& Campaign,
    GameSession& Session,
    GameAudioRuntime& Audio,
    ClientRuntime& Client,
    const std::string& MapPath,
    bool PreserveManualCamera)
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
        [](int ChkUnitID, uint32_t ImageID) {
            return Game::Systems::ZergSystem::CanUnitBurrow(
                ChkUnitID, ImageID);
        },
        [&Client](int ChkUnitID) {
            Game::Components::UnitCollisionBounds Bounds;
            auto DataAssets = Client.GetDataAssets();
            if (!DataAssets.HasCache())
                return Bounds;

            const auto* DataLoader = DataAssets.GetDataLoader();
            const auto* UnitData = DataLoader ? DataLoader->FindUnitByID((uint32_t)ChkUnitID) : nullptr;
            if (!UnitData)
                return Bounds;

            Bounds.IsValid = UnitData->DimensionWidth() > 0 && UnitData->DimensionHeight() > 0;
            Bounds.Left = UnitData->DimensionLeft;
            Bounds.Up = UnitData->DimensionUp;
            Bounds.Right = UnitData->DimensionRight;
            Bounds.Down = UnitData->DimensionDown;
            return Bounds;
        },
        [&Session, &Client, PreserveManualCamera](auto Position) {
            if (PreserveManualCamera)
                return;
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
