#pragma once

#include <cstdint>
#include <ctime>

#include "Client/ClientRuntime.h"
#include "Main/GameLoop/HudResourceSync.h"
#include "Main/CampaignRuntime.h"
#include "Main/GameCampaignBootstrap.h"
#include "Main/GameAudioRuntime.h"
#include "Main/GameEntitySpawnRuntime.h"
#include "Main/GameSession.h"
#include "Main/GameSessionBootstrap.h"
#include "UI/Hud.h"

namespace Game::Client::GameLoop
{

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
    GameEntitySpawnRuntime::SpawnChkEntities(Session, ResolveChkImageID, ResolveChkSubunitImageID);

    auto SpawnResourceForStart = [&Session, &Client](int ChkUnitId, int WorldX, int WorldY, int Amount)
    {
        auto ResolveImageID = [&Client](int ChkUnitID) {
            return Client.GetSpriteAssets().ResolveImageID(ChkUnitID);
        };
        GameEntitySpawnRuntime::SpawnResourceEntity(
            Session, ChkUnitId, WorldX, WorldY, Amount, ResolveImageID);
    };
    auto IsMineralStartUnit = [](int ChkUnitId) { return IsMineralUnit(ChkUnitId); };
    auto IsGeyserStartUnit = [](int ChkUnitId) { return IsGeyserUnit(ChkUnitId); };
    GameSessionBootstrap::EnsurePlayableStart(
        Session, 0, SpawnResourceForStart, IsMineralStartUnit, IsGeyserStartUnit);
    GameSessionBootstrap::EnsurePlayableStart(
        Session, 1, SpawnResourceForStart, IsMineralStartUnit, IsGeyserStartUnit);

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
    const std::string& MapPath)
{
    if (!Session.IsMapLoaded())
        return;

    InitializeEcsMapAndGameplaySystems(Session, Client, Audio);
    InitializeGameplayEntitiesAndRuntimeState(Session, Client, Hud);
    InitializeCampaignRuntimeForSession(Campaign, Session, Audio, Client, MapPath);
}

} // namespace Game::Client::GameLoop
