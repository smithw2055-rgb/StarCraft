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
    bool AddPlayableStarts,
    bool RegisterGenericAi)
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

    GameSessionBootstrap::RebuildPlayerSupplyAndRegisterAi(
        Session, RegisterGenericAi);

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
    const bool IsCampaignMission =
        GameSessionBootstrap::Detail::IsCampaignMissionPath(MapPath);
    const char* ShowcaseValue = std::getenv("STARCRAFT_AI_SHOWCASE");
    const bool RunShowcase = ShowcaseValue && ShowcaseValue[0] != '\0' &&
                             ShowcaseValue[0] != '0';
    const bool IsObserver =
        GameSessionBootstrap::Detail::IsAiObserverMatch();
    const bool AddPlayableStarts = !IsCampaignMission || RunShowcase || IsObserver;
    const bool RegisterGenericAi = (!IsCampaignMission && !RunShowcase) || IsObserver;
    InitializeGameplayEntitiesAndRuntimeState(
        Session, Client, Hud, AddPlayableStarts, RegisterGenericAi);

    const int LocalPlayerID = Session.GetLocalPlayerID();
    GameSessionBootstrap::Detail::StartRace PlayerRace =
        GameSessionBootstrap::Detail::RaceFromScriptPlayer(Session, LocalPlayerID);
    PlayerRace = GameSessionBootstrap::Detail::ApplyShowcaseRaceOverride(PlayerRace);

    if (PlayerRace == GameSessionBootstrap::Detail::StartRace::Terran)
    {
        for (int Eid : Session.Entities().GetEntitiesWith<Game::Components::UnitComponent,
                                                          Game::Components::PlayerComponent>())
        {
            auto* P = Session.Entities().GetComponent<Game::Components::PlayerComponent>(Eid);
            auto* U = Session.Entities().GetComponent<Game::Components::UnitComponent>(Eid);
            if (P && U && P->PlayerID == LocalPlayerID)
            {
                if (U->UnitRace == Game::Components::Race::Protoss)
                {
                    PlayerRace = GameSessionBootstrap::Detail::StartRace::Protoss;
                    break;
                }
                else if (U->UnitRace == Game::Components::Race::Zerg)
                {
                    PlayerRace = GameSessionBootstrap::Detail::StartRace::Zerg;
                    break;
                }
            }
        }
    }

    std::string ConsoleRace = "terran";
    if (PlayerRace == GameSessionBootstrap::Detail::StartRace::Protoss)
        ConsoleRace = "protoss";
    else if (PlayerRace == GameSessionBootstrap::Detail::StartRace::Zerg)
        ConsoleRace = "zerg";

    Client.SetConsoleRace(ConsoleRace);
    Core::Utils::Logger::Info("GameBootstrap", "Selected console race: " + ConsoleRace);

    const bool EnableCampaign = EnableCampaignRuntime;
    if (EnableCampaign)
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
