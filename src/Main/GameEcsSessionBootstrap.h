#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <string>

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
    UI::Hud& Hud,
    bool AddPlayableStarts)
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
    if (AddPlayableStarts)
    {
        for (int PlayerID = 0; PlayerID < 8; ++PlayerID)
            GameSessionBootstrap::EnsurePlayableStart(
                Session, PlayerID, SpawnResourceForStart, IsMineralStartUnit, IsGeyserStartUnit);
    }

    GameSessionBootstrap::RebuildPlayerSupplyAndRegisterAi(Session);

    Session.MarkEcsInitialized();
    SyncHudFromPlayerResources(Hud, Session.GetLocalPlayerID());

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
    std::string NormalizedMapPath = MapPath;
    std::replace(NormalizedMapPath.begin(), NormalizedMapPath.end(), '\\', '/');
    std::transform(NormalizedMapPath.begin(), NormalizedMapPath.end(), NormalizedMapPath.begin(),
        [](unsigned char C) { return (char)std::tolower(C); });
    const bool IsCampaignMission =
        NormalizedMapPath.find("/campaign/") != std::string::npos;
    InitializeGameplayEntitiesAndRuntimeState(Session, Client, Hud, !IsCampaignMission);
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
