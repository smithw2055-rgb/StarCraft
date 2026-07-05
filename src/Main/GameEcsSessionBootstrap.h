#pragma once

#include <cstdint>
#include <ctime>

#include "Client/ClientRuntime.h"
#include "Core/Utils/Logger.h"
#include "Main/GameLoop/HudResourceSync.h"
#include "Main/CampaignRuntime.h"
#include "Main/GameCampaignBootstrap.h"
#include "Main/GameAudioRuntime.h"
#include "Main/GameEntitySpawnRuntime.h"
#include "Main/GameSession.h"
#include "Main/GameSessionBootstrap.h"
#include "Resource/GameDataLoader.h"
#include "UI/Hud.h"

namespace Game::Client::GameLoop
{
inline bool HasExistingOwnedUnits(GameSession& Session, int PlayerId)
{
    auto& EM = Session.Entities();
    for (int EntityId : EM.GetEntitiesWith<Game::Components::UnitComponent,
                                          Game::Components::PlayerComponent>())
    {
        auto* Player = EM.GetComponent<Game::Components::PlayerComponent>(EntityId);
        auto* Resource = EM.GetComponent<Game::Components::ResourceComponent>(EntityId);
        if (Player && Player->PlayerID == PlayerId && !Resource)
            return true;
    }
    return false;
}

inline void InitializeEcsMapAndGameplaySystems(
    GameSession& Session,
    ClientRuntime& Client,
    GameAudioRuntime& Audio)
{
    auto* TilesetData = Session.HasTilesetName()
        ? Client.GetTilesetAssets().GetTilesetData(Session.GetTilesetName())
        : nullptr;
    Session.BuildEcsMapFromLoadedMap(TilesetData);

    Audio.RegisterGameplaySystems(Session);
}

inline void InitializeGameplayEntitiesAndRuntimeState(
    GameSession& Session,
    ClientRuntime& Client,
    UI::Hud& Hud)
{
    auto ResolveChkImageID = [&Client](int ChkUnitID) {
        return Client.GetSpriteAssets().ResolveImageID(ChkUnitID);
    };
    auto ResolveChkSubunitImageID = [&Client](int ChkUnitID) {
        return Client.GetSpriteAssets().ResolveSubunitImageID(ChkUnitID);
    };
    auto ResolveChkCollisionBounds = [&Client](int ChkUnitID) {
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
    };
    GameEntitySpawnRuntime::SpawnChkEntities(
        Session, ResolveChkImageID, ResolveChkSubunitImageID, ResolveChkCollisionBounds);

    auto SpawnResourceForStart = [&Session, &Client, &ResolveChkCollisionBounds](
        int ChkUnitId, int WorldX, int WorldY, int Amount)
    {
        auto ResolveImageID = [&Client](int ChkUnitID) {
            return Client.GetSpriteAssets().ResolveImageID(ChkUnitID);
        };
        GameEntitySpawnRuntime::SpawnResourceEntity(
            Session, ChkUnitId, WorldX, WorldY, Amount,
            ResolveImageID, ResolveChkCollisionBounds);
    };
    auto IsMineralStartUnit = [](int ChkUnitId) { return IsMineralUnit(ChkUnitId); };
    auto IsGeyserStartUnit = [](int ChkUnitId) { return IsGeyserUnit(ChkUnitId); };
    GameSessionBootstrap::EnsurePlayableStart(
        Session, 0, SpawnResourceForStart, IsMineralStartUnit, IsGeyserStartUnit);
    if (Session.GetChkUnits().empty() || HasExistingOwnedUnits(Session, 1))
    {
        GameSessionBootstrap::EnsurePlayableStart(
            Session, 1, SpawnResourceForStart, IsMineralStartUnit, IsGeyserStartUnit);
    }

    GameSessionBootstrap::RebuildPlayerSupplyAndRegisterAi(Session);

    Session.MarkEcsInitialized();
    SyncHudFromPlayerResources(Hud);

    Session.RebuildRegionsFromEcsMap();
    Session.StartDefaultReplayRecording((uint32_t)std::time(nullptr));
}

inline void InitializeLoadedMapGameplayRuntime(
    GameSession& Session,
    ClientRuntime& Client,
    GameAudioRuntime& Audio,
    UI::Hud& Hud,
    CampaignRuntime& Campaign,
    const std::string& MapPath,
    bool EnableCampaignRuntime,
    bool PreserveManualCamera = false)
{
    if (!Session.IsMapLoaded())
        return;

    InitializeEcsMapAndGameplaySystems(Session, Client, Audio);
    InitializeGameplayEntitiesAndRuntimeState(Session, Client, Hud);
    if (EnableCampaignRuntime)
    {
        InitializeCampaignRuntimeForSession(
            Campaign, Session, Audio, Client, MapPath, PreserveManualCamera);
    }
    else
    {
        Campaign.Deactivate();
        Core::Utils::Logger::Info(
            "GameController",
            "Campaign runtime disabled for custom map: " + MapPath);
    }
}

} // namespace Game::Client::GameLoop
