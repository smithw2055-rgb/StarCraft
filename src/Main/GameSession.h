#pragma once

#include <cstdio>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "Client/MapLoader.h"
#include "Core/ECS/SoAComponentStream.h"
#include "Core/ECS/SystemManager.h"
#include "Core/ECS/WorldSnapshot.h"
#include "Game/Networking/RollbackStateManager.h"
// === Consolidated from src/Game/BWAPI/BWAPIInterface.h ===


#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include <cmath>

namespace Game::BWAPI
{

enum class BW_UnitType : int
{
    Terran_Marine = 0,
    Terran_Ghost,
    Terran_Vulture,
    Terran_Goliath,
    Terran_Siege_Tank_Tank_Mode = 5,
    Terran_SCV = 7,
    Terran_Wraith,
    Terran_Science_Vessel,
    Terran_Dropship = 11,
    Terran_Battlecruiser,
    Terran_Firebat = 32,
    Terran_Medic = 34,
    Terran_Command_Center = 106,
    Terran_Comsat_Station,
    Terran_Nuclear_Silo,
    Terran_Supply_Depot,
    Terran_Refinery,
    Terran_Barracks,
    Terran_Academy,
    Terran_Factory,
    Terran_Starport,
    Terran_Control_Tower,
    Terran_Science_Facility,
    Terran_Covert_Ops,
    Terran_Physics_Lab,
    Terran_Machine_Shop = 122,
    Terran_Engineering_Bay = 124,
    Terran_Armory,
    Terran_Missile_Turret,
    Terran_Bunker,
    Zerg_Larva = 35,
    Zerg_Egg,
    Zerg_Zergling,
    Zerg_Hydralisk,
    Zerg_Ultralisk,
    Zerg_Broodling,
    Zerg_Drone,
    Zerg_Overlord,
    Zerg_Mutalisk,
    Zerg_Guardian,
    Zerg_Queen,
    Zerg_Defiler,
    Zerg_Scourge,
    Zerg_Infested_Terran = 50,
    Zerg_Cocoon = 59,
    Zerg_Devourer = 62,
    Zerg_Lurker = 103,
    Zerg_Hatchery = 131,
    Zerg_Lair,
    Zerg_Hive,
    Zerg_Nydus_Canal,
    Zerg_Hydralisk_Den,
    Zerg_Defiler_Mound,
    Zerg_Greater_Spire,
    Zerg_Queens_Nest,
    Zerg_Evolution_Chamber,
    Zerg_Ultralisk_Cavern,
    Zerg_Spire,
    Zerg_Spawning_Pool,
    Zerg_Creep_Colony,
    Zerg_Spore_Colony,
    Zerg_Sunken_Colony = 146,
    Zerg_Extractor = 149,
    Protoss_Corsair = 60,
    Protoss_Dark_Templar,
    Protoss_Dark_Archon = 63,
    Protoss_Probe = 64,
    Protoss_Zealot,
    Protoss_Dragoon,
    Protoss_High_Templar,
    Protoss_Archon,
    Protoss_Shuttle,
    Protoss_Scout,
    Protoss_Arbiter,
    Protoss_Carrier,
    Protoss_Interceptor,
    Protoss_Reaver = 83,
    Protoss_Observer,
    Protoss_Scarab,
    Protoss_Nexus = 154,
    Protoss_Robotics_Facility,
    Protoss_Pylon,
    Protoss_Assimilator,
    Protoss_Observatory = 159,
    Protoss_Gateway = 160,
    Protoss_Photon_Cannon = 162,
    Protoss_Citadel_of_Adun,
    Protoss_Cybernetics_Core,
    Protoss_Templar_Archives,
    Protoss_Forge,
    Protoss_Stargate,
    Protoss_Fleet_Beacon = 169,
    Protoss_Arbiter_Tribunal,
    Protoss_Robotics_Support_Bay,
    Protoss_Shield_Battery,
    Resource_Mineral_Field = 176,
    Resource_Vespene_Geyser = 188,
    None = 228
};

enum class BW_Player
{
    Neutral = 0,
    Player1, Player2, Player3, Player4,
    Player5, Player6, Player7, Player8,
    Player9, Player10, Player11, Player12
};

enum class BW_GameType
{
    Melee,
    FreeForAll,
    OneVsOne,
    CaptureTheFlag,
    Greed,
    Slaughter,
    SuddenDeath,
    Ladder,
    UseMapSettings,
    TeamMelee,
    TeamFreeForAll,
    TeamCaptureTheFlag,
    TopVsBottom,
    None
};

enum class BW_EventType
{
    MatchStart,
    MatchEnd,
    MatchFrame,
    UnitCreate,
    UnitDestroy,
    UnitMorph,
    UnitRenegade,
    UnitComplete,
    UnitDiscover,
    UnitEvade,
    UnitShow,
    UnitHide,
    SaveGame,
    MenuFrame,
    None
};

struct BW_Position
{
    int X = 0;
    int Y = 0;

    BW_Position() = default;
    BW_Position(int X, int Y) : X(X), Y(Y) {}

    bool operator==(const BW_Position& Other) const { return X == Other.X && Y == Other.Y; }
    int GetApproxDistance(const BW_Position& Other) const
    {
        int dx = std::abs(X - Other.X);
        int dy = std::abs(Y - Other.Y);
        return (dx > dy) ? dx : dy;
    }
};

struct BW_Unit
{
    int ID = -1;
    BW_UnitType Type = BW_UnitType::None;
    BW_Position Position;
    int HitPoints = 0;
    int Shields = 0;
    int Energy = 0;
    int PlayerID = 0;
    bool IsCompleted = false;
    bool IsVisible = false;
    bool IsSelected = false;
};

struct BW_GameInfo
{
    int MapWidth = 0;
    int MapHeight = 0;
    int FrameCount = 0;
    int ElapsedTime = 0;
    bool IsPaused = false;
    BW_GameType GameType = BW_GameType::None;
};

using BW_FrameCallback = std::function<void(BW_GameInfo&)>;
using BW_UnitCallback = std::function<void(const BW_Unit&)>;
using BW_EventCallback = std::function<void(BW_EventType, const BW_GameInfo&)>;

class BWAPIInterface
{
public:
    BWAPIInterface() = default;

    void SetFrameCallback(BW_FrameCallback Callback) { mOnFrame = std::move(Callback); }
    void SetUnitCreatedCallback(BW_UnitCallback Callback) { mOnUnitCreate = std::move(Callback); }
    void SetUnitDestroyedCallback(BW_UnitCallback Callback) { mOnUnitDestroy = std::move(Callback); }
    void SetEventCallback(BW_EventCallback Callback) { mOnEvent = std::move(Callback); }

    void OnGameStart() { mGameInfo.FrameCount = 0; mGameInfo.ElapsedTime = 0; }
    void OnGameEnd() { if (mOnEvent) mOnEvent(BW_EventType::MatchEnd, mGameInfo); }
    void OnFrame()
    {
        ++mGameInfo.FrameCount;
        mGameInfo.ElapsedTime = mGameInfo.FrameCount / 24;
        if (mOnFrame) mOnFrame(mGameInfo);
    }

    void RegisterUnit(const BW_Unit& Unit)
    {
        mUnits[Unit.ID] = Unit;
        if (mOnUnitCreate) mOnUnitCreate(Unit);
    }

    void UnregisterUnit(int UnitID)
    {
        auto it = mUnits.find(UnitID);
        if (it != mUnits.end())
        {
            if (mOnUnitDestroy) mOnUnitDestroy(it->second);
            mUnits.erase(it);
        }
    }

    void UpdateUnit(const BW_Unit& Unit)
    {
        mUnits[Unit.ID] = Unit;
    }

    const BW_Unit* GetUnit(int UnitID) const
    {
        auto it = mUnits.find(UnitID);
        return (it != mUnits.end()) ? &it->second : nullptr;
    }

    std::vector<const BW_Unit*> GetUnitsInRectangle(int Left, int Top, int Right, int Bottom) const
    {
        std::vector<const BW_Unit*> result;
        for (const auto& [id, unit] : mUnits)
            if (unit.Position.X >= Left && unit.Position.X <= Right &&
                unit.Position.Y >= Top && unit.Position.Y <= Bottom)
                result.push_back(&unit);
        return result;
    }

    BW_GameInfo& GetGameInfo() { return mGameInfo; }
    const BW_GameInfo& GetGameInfo() const { return mGameInfo; }

    // --- AI Module Interface ---
    struct AIModule
    {
        virtual ~AIModule() = default;
        virtual void OnStart() {}
        virtual void OnEnd(bool IsWinner) { (void)IsWinner; }
        virtual void OnFrame() {}
        virtual void OnUnitCreate(const BW_Unit&) {}
        virtual void OnUnitDestroy(const BW_Unit&) {}
        virtual void OnSendText(const std::string&) {}
        virtual void OnReceiveText(int, const std::string&) {}
    };

    void SetAIModule(AIModule* Module) { mAI = Module; }
    AIModule* GetAIModule() { return mAI; }

private:
    BW_GameInfo mGameInfo;
    std::unordered_map<int, BW_Unit> mUnits;
    BW_FrameCallback mOnFrame;
    BW_UnitCallback mOnUnitCreate;
    BW_UnitCallback mOnUnitDestroy;
    BW_EventCallback mOnEvent;
    AIModule* mAI = nullptr;
};

} // namespace Game::BWAPI

#include "Game/IScriptVM.h"
#include "Game/Map/Map.h"
#include "Game/Networking/LockstepSync.h"
#include "Game/Networking/SyncTransport.h"
#include "Engine/EngineApp.h"
#include "Game/Replay.h"
#include "Game/Trigger/SCRTriggerParser.h"

namespace Game::Client::GameLoop
{
#ifndef STARCRAFT_MATCH_OUTCOME_DEFINED
#define STARCRAFT_MATCH_OUTCOME_DEFINED
enum class MatchOutcome : uint8_t
{
    Playing = 0,
    Victory = 1,
    Defeat = 2,
    Draw = 3
};
#endif
} // namespace Game::Client::GameLoop


namespace Game::Systems { class FogOfWarSystem; }

namespace Game::Client::GameLoop
{

inline int ResolveLocalPlayerID(const Game::Trigger::SCRTriggerData& ScriptData)
{
    for (int PlayerID = 0; PlayerID < 8; ++PlayerID)
        if (ScriptData.Players[(size_t)PlayerID].Owner == 6)
            return PlayerID;
    return 0;
}

struct GameSession
{
    Core::ECS::EntityManager& Entities()
    {
        return EcsSysMgr.GetEntityManager();
    }

    Core::ECS::SystemManager& Systems()
    {
        return EcsSysMgr;
    }

    void UpdateSystems(float DeltaSeconds)
    {
        EcsSysMgr.UpdateAll(DeltaSeconds);
        KinematicsStream.ExtractFromECS(EcsSysMgr.GetEntityManager());
    }

    Engine::EngineApp& EngineHost() noexcept { return HostedEngineApp; }
    const Engine::EngineApp& EngineHost() const noexcept { return HostedEngineApp; }
    Engine::EngineContext& EngineContext() noexcept { return HostedEngineApp.Context(); }
    const Engine::EngineContext& EngineContext() const noexcept { return HostedEngineApp.Context(); }

    void UpdateSimulationStep(float DeltaSeconds)
    {
        if (RollbackRecordingEnabled)
        {
            RollbackMgr.RecordFrame(
                EcsSysMgr.GetEntityManager(),
                static_cast<uint32_t>(SimulationTick));
            HostedEngineApp.RollbackBuffer().RecordTick(
                EcsSysMgr.GetEntityManager(),
                static_cast<uint32_t>(SimulationTick));
        }
        HostedEngineApp.Context().Time().StepExactTicks(1u);
        EcsSysMgr.UpdateSimulationStep(DeltaSeconds);
        KinematicsStream.ExtractFromECS(EcsSysMgr.GetEntityManager());
    }

    void UpdatePresentationFrame(float DeltaSeconds)
    {
        HostedEngineApp.Context().Assets().FlushPendingGpuUploads(4u * 1024u * 1024u);
        HostedEngineApp.Context().Audio2D().FlushFrame({0.0f, 0.0f}, 1600.0f, DeltaSeconds);
        HostedEngineApp.Context().SceneTree().PropagateAllTransforms();
        EcsSysMgr.UpdatePresentationFrame(DeltaSeconds);
    }

    GameMap& MutableMapData()
    {
        return MapData;
    }

    const GameMap& GetMapData() const
    {
        return MapData;
    }

    void SaveTransformTickState()
    {
        EcsSysMgr.GetEntityManager().ForEachComponent<Game::Components::TransformComponent>(
            [](Core::ECS::EntityID, Game::Components::TransformComponent& Trans)
            {
                Trans.PrevPosition = Trans.Position;
                Trans.HasPrevPosition = true;
            });
    }

    size_t SyncCreepPresentation()
    {
        const size_t TileCount = (size_t)MapData.Width * (size_t)MapData.Height;
        if (MapData.Creep.size() != TileCount)
            MapData.Creep.resize(TileCount, 0);

        size_t ChangedTiles = 0;
        for (int Y = 0; Y < MapData.Height; ++Y)
        {
            const size_t RowBase = (size_t)Y * (size_t)MapData.Width;
            for (int X = 0; X < MapData.Width; ++X)
            {
                const uint8_t HasCreep = EcsMap.GetTile(X, Y).HasCreep ? 1 : 0;
                uint8_t& Dest = MapData.Creep[RowBase + (size_t)X];
                if (Dest != HasCreep)
                {
                    Dest = HasCreep;
                    ++ChangedTiles;
                }
            }
        }
        LastCreepSyncChangedTiles = ChangedTiles;
        return ChangedTiles;
    }

    Game::Map::GameMap& MutableEcsMap()
    {
        return EcsMap;
    }

    const Game::Map::GameMap& GetEcsMap() const
    {
        return EcsMap;
    }

    std::vector<Game::Map::PlacedUnit>& MutableChkUnits()
    {
        return ChkUnits;
    }

    const std::vector<Game::Map::PlacedUnit>& GetChkUnits() const
    {
        return ChkUnits;
    }

    std::vector<Game::Map::PlacedDoodad>& MutableChkDoodads()
    {
        return ChkDoodads;
    }

    const std::vector<Game::Map::PlacedDoodad>& GetChkDoodads() const
    {
        return ChkDoodads;
    }

    Game::Trigger::SCRTriggerData& MutableScriptData()
    {
        return ScrTriggerData;
    }

    const Game::Trigger::SCRTriggerData& GetScriptData() const
    {
        return ScrTriggerData;
    }

    int GetLocalPlayerID() const
    {
        return ResolveLocalPlayerID(ScrTriggerData);
    }

    void ConfigureSyntheticSkirmishMap()
    {
        MapData.Width = 64;
        MapData.Height = 64;
        MapData.MegaTileIds.resize((size_t)MapData.Width * MapData.Height);
        for (int Y = 0; Y < MapData.Height; ++Y)
        {
            for (int X = 0; X < MapData.Width; ++X)
            {
                const int Group = 1 + ((X / 4) + (Y / 4) * 3 + ((X + Y) / 11)) % 48;
                const int Subtile = (X + Y * 3) & 0x0F;
                MapData.MegaTileIds[(size_t)Y * MapData.Width + X] =
                    (uint16_t)((Group << 4) | Subtile);
            }
        }
        MapData.Walkable.assign((size_t)MapData.Width * MapData.Height, 1);
        MapData.Creep.assign((size_t)MapData.Width * MapData.Height, 0);
        MapData.StartLocations.clear();
        MapData.StartLocations.push_back({16, 18, 0});
        MapData.StartLocations.push_back({48, 46, 1});
        MapData.TilesetName = "badlands";
        ChkUnits.clear();
        ChkDoodads.clear();
        ScrTriggerData = {};
    }

    void ApplyLoadedMap(const Game::Client::MapLoader::Result& Result)
    {
        MapData = Result.Map;
        ChkUnits = Result.ChkUnits;
        ChkDoodads = Result.ChkDoodads;
        ScrTriggerData = {};
    }

    bool ParseLoadedScriptData(const std::vector<uint8_t>& RawChkData)
    {
        Game::Trigger::SCRTriggerParser Parser;
        return Parser.Parse(RawChkData, ScrTriggerData);
    }

    std::string GetTilesetNameOrDefault() const
    {
        return MapData.TilesetName.empty() ? "badlands" : MapData.TilesetName;
    }

    bool HasTilesetName() const
    {
        return !MapData.TilesetName.empty();
    }

    const std::string& GetTilesetName() const
    {
        return MapData.TilesetName;
    }

    std::pair<float, float> GetInitialCameraPosition() const
    {
        float CameraX = (float)(MapData.Width * 32) * 0.5f;
        float CameraY = (float)(MapData.Height * 32) * 0.5f;
        const int LocalPlayerID = GetLocalPlayerID();
        for (const auto& StartLocation : MapData.StartLocations)
        {
            if (StartLocation.PlayerId == LocalPlayerID)
            {
                CameraX = (float)(StartLocation.TileX * 32 + 16);
                CameraY = (float)(StartLocation.TileY * 32 + 16);
                break;
            }
        }
        return {CameraX, CameraY};
    }

    template <typename TilesetDataT>
    void BuildEcsMapFromLoadedMap(const TilesetDataT* TilesetData)
    {
        EcsMap.Generate(MapData.Width, MapData.Height);
        EcsMap.GetStartLocations().clear();
        for (const auto& SL : MapData.StartLocations)
        {
            Game::Map::StartLocationInfo Info;
            Info.TileX = SL.TileX;
            Info.TileY = SL.TileY;
            Info.PlayerIndex = SL.PlayerId;
            EcsMap.GetStartLocations().push_back(Info);
        }

        const size_t EcsTileCount = (size_t)MapData.Width * (size_t)MapData.Height;
        if (MapData.MegaTileIds.size() >= EcsTileCount)
        {
            for (int Y = 0; Y < MapData.Height; ++Y)
            {
                for (int X = 0; X < MapData.Width; ++X)
                {
                    const size_t Index = (size_t)Y * (size_t)MapData.Width + (size_t)X;
                    EcsMap.SetMegaTile(X, Y, MapData.MegaTileIds[Index]);
                }
            }
        }

        if (TilesetData && TilesetData->Loaded)
            EcsMap.ApplyTilesetData(&TilesetData->Cv5, &TilesetData->Vf4);

        if (MapData.Walkable.size() >= EcsTileCount)
        {
            for (int Y = 0; Y < MapData.Height; ++Y)
            {
                for (int X = 0; X < MapData.Width; ++X)
                {
                    const size_t Index = (size_t)Y * (size_t)MapData.Width + (size_t)X;
                    MapData.Walkable[Index] = EcsMap.IsWalkable(X, Y) ? 1 : 0;
                }
            }
        }
    }

    void RebuildRegionsFromEcsMap()
    {
        auto& Nav2D = HostedEngineApp.Context().Navigation2D();
        const int W = std::max(1, EcsMap.GetWidth());
        const int H = std::max(1, EcsMap.GetHeight());
        Nav2D.InitializeGrid(W, H, 32.0f);
        for (int Y = 0; Y < H; ++Y)
        {
            for (int X = 0; X < W; ++X)
            {
                Nav2D.SetCellBlocked(X, Y, !EcsMap.IsWalkable(X, Y));
                Nav2D.SetCellElevation(X, Y, static_cast<uint8_t>(EcsMap.GetGroundHeight(X, Y)), false);
            }
        }
        Nav2D.RebuildConnectedRegions(1u);
    }

    Engine::Navigation::NavigationServer2D& Navigation() noexcept
    {
        return HostedEngineApp.Context().Navigation2D();
    }

    Game::BWAPI::BWAPIInterface& Bwapi()
    {
        return BwapiInterface;
    }

    void SetFogSystem(Game::Systems::FogOfWarSystem* System)
    {
        FogSystem = System;
    }

    Game::Systems::FogOfWarSystem* GetFogSystem()
    {
        return FogSystem;
    }

    bool IsNetworkEnabled() const
    {
        return NetSyncEnabled;
    }

    bool IsNetworkHost() const
    {
        return IsHost;
    }

    bool HostNetworkGame(const std::string& BindIP, int Port, int MaxClients)
    {
        if (!NetTransport.Init())
            return false;
        if (!NetTransport.StartHost(BindIP, Port, MaxClients))
            return false;

        NetSync.Init((uint8_t)(MaxClients + 1), 2);
        NetSyncEnabled = true;
        IsHost = true;
        ConnectNetworkCallbacks();
        return true;
    }

    bool JoinNetworkGame(const std::string& HostIP, int Port)
    {
        if (!NetTransport.Init())
            return false;
        if (!NetTransport.Connect(HostIP, Port))
            return false;

        NetSync.Init(8, 2);
        NetSyncEnabled = true;
        IsHost = false;
        ConnectNetworkCallbacks();
        return true;
    }

    void PollNetworkMessages()
    {
        NetTransport.PollMessages();
    }

    void FlushNetworkOutgoingMessages()
    {
        for (uint32_t CID = 0; CID < 16; ++CID)
        {
            while (NetSync.HasPendingMessages(CID))
            {
                auto Msg = NetSync.GetNextOutgoingMsg(CID);
                if (!Msg.empty())
                    NetTransport.SendToClient(CID, Msg);
                else
                    break;
            }
        }
    }

    template <typename ActionHandlerT>
    void UpdateNetworkFrame(ActionHandlerT&& ActionHandler)
    {
        NetSync.CheckTimeouts(30000);
        NetSync.AdvanceFrame(std::forward<ActionHandlerT>(ActionHandler));
    }

    template <typename ActionHandlerT>
    void ProcessNetworkFrame(ActionHandlerT&& ActionHandler)
    {
        if (!NetSyncEnabled)
            return;

        PollNetworkMessages();
        FlushNetworkOutgoingMessages();
        UpdateNetworkFrame(std::forward<ActionHandlerT>(ActionHandler));
    }

    void StartNetworkGameIfEnabled()
    {
        if (NetSyncEnabled)
            NetSync.StartGame();
    }

    void ScheduleNetworkAction(uint8_t PlayerID, const std::vector<uint8_t>& ActionData)
    {
        if (NetSyncEnabled)
            NetSync.ScheduleAction(PlayerID, ActionData);
    }

    void ScheduleNetworkAction(uint8_t PlayerID,
                               uint8_t ActionType,
                               uint16_t P1,
                               uint16_t P2,
                               uint16_t P3,
                               uint8_t P4)
    {
        if (!NetSyncEnabled)
            return;

        std::vector<uint8_t> ActionData;
        ActionData.push_back(ActionType);
        ActionData.push_back((uint8_t)P1);
        ActionData.push_back((uint8_t)(P1 >> 8));
        ActionData.push_back((uint8_t)P2);
        ActionData.push_back((uint8_t)(P2 >> 8));
        ActionData.push_back((uint8_t)P3);
        ActionData.push_back((uint8_t)(P3 >> 8));
        ActionData.push_back(P4);
        ScheduleNetworkAction(PlayerID, ActionData);
    }

    bool IsEcsInitialized() const
    {
        return EcsInitialized;
    }

    void MarkEcsInitialized()
    {
        EcsInitialized = true;
    }

    bool IsMapLoaded() const
    {
        return MapLoaded;
    }

    void MarkMapLoaded()
    {
        MapLoaded = true;
        SimulationAccumulatorSeconds = 0.0;
        SimulationTick = 0;
    }

    bool HasTerranAuditSpawned() const
    {
        return TerranAuditSpawned;
    }

    void MarkTerranAuditSpawned()
    {
        TerranAuditSpawned = true;
    }

    bool IsReplayRecording() const
    {
        return ReplayRecording;
    }

    void StartReplayRecording(const Game::Replay::ReplayHeader& Header)
    {
        ReplayWriter.Init(Header, {});
        ReplayRecording = true;
    }

    void StartDefaultReplayRecording(uint32_t RandomSeed)
    {
        Game::Replay::ReplayHeader Header;
        constexpr char DefaultName[] = "StarCraft Remake Game";
        for (size_t I = 0; I < sizeof(Header.GameName); ++I)
            Header.GameName[I] = '\0';
        for (size_t I = 0; I + 1 < sizeof(Header.GameName) && DefaultName[I] != '\0'; ++I)
            Header.GameName[I] = DefaultName[I];

        Header.MapWidth = (uint16_t)EcsMap.GetWidth();
        Header.MapHeight = (uint16_t)EcsMap.GetHeight();
        Header.RandomSeed = RandomSeed;
        Header.PlayerCount = 2;
        Header.ControllerType[0] = 1;
        Header.ControllerType[1] = 2;
        Header.RaceType[0] = 1;
        Header.RaceType[1] = 0;
        StartReplayRecording(Header);
    }

    void RecordReplayAction(uint32_t Frame,
                            uint8_t PlayerID,
                            uint8_t ActionType,
                            uint16_t P1,
                            uint16_t P2,
                            uint16_t P3,
                            uint8_t P4)
    {
        ReplayWriter.RecordAction(Frame, PlayerID, ActionType, P1, P2, P3, P4);
    }

    void RecordLocalFrameAction(uint32_t Frame,
                                uint8_t PlayerID,
                                uint8_t ActionType,
                                uint16_t P1,
                                uint16_t P2,
                                uint16_t P3,
                                uint8_t P4)
    {
        if (IsReplayRecording())
            RecordReplayAction(Frame, PlayerID, ActionType, P1, P2, P3, P4);

        ScheduleNetworkAction(PlayerID, ActionType, P1, P2, P3, P4);
    }

    std::vector<uint8_t> SerializeReplay() const
    {
        return ReplayWriter.SerializeReplay();
    }

    size_t GetLastReplayActionCount() const
    {
        return ReplayActions.size();
    }

    bool SaveReplayToFile(const std::string& FilePath, size_t* OutBytesWritten = nullptr) const
    {
        auto Data = SerializeReplay();
        if (Data.empty())
            return false;

        FILE* F = fopen(FilePath.c_str(), "wb");
        if (!F)
            return false;
        fwrite(Data.data(), 1, Data.size(), F);
        fclose(F);

        if (OutBytesWritten)
            *OutBytesWritten = Data.size();
        return true;
    }

    bool LoadReplayFromFile(const std::string& FilePath, std::string* OutGameName = nullptr)
    {
        FILE* F = fopen(FilePath.c_str(), "rb");
        if (!F)
            return false;

        fseek(F, 0, SEEK_END);
        size_t Size = ftell(F);
        fseek(F, 0, SEEK_SET);

        std::vector<uint8_t> FileData(Size);
        fread(FileData.data(), 1, Size, F);
        fclose(F);

        Game::Replay::ReplayReader Reader;
        if (!Reader.LoadReplay(FileData))
            return false;

        if (OutGameName)
            *OutGameName = Reader.GetHeader().GameName;
        StartReplayPlayback(Reader.GetActions());
        return true;
    }

    void StartReplayPlayback(const std::vector<Game::Replay::ReplayAction>& Actions)
    {
        ReplayActions = Actions;
        ReplayFrame = 0;
        ReplayPlaying = true;
    }

    void ConnectNetworkCallbacks()
    {
        NetTransport.SetOnMessage([this](const Game::Networking::TransportMessage& Msg) {
            NetSync.ProcessMessage(Msg.ClientID, Msg.Payload.data(), Msg.Payload.size());
        });

        NetTransport.SetOnConnect([this](uint32_t ClientID, bool Connected) {
            if (Connected)
                NetSync.OnClientConnected(ClientID);
            else
                NetSync.OnClientDisconnected(ClientID);
        });
    }

    GameMap MapData;
    std::vector<Game::Map::PlacedUnit> ChkUnits;
    std::vector<Game::Map::PlacedDoodad> ChkDoodads;
    Engine::EngineApp HostedEngineApp{1.0f / 24.0f, 4};
    Core::ECS::SystemManager& EcsSysMgr = HostedEngineApp.PrimaryWorld();
    Game::Map::GameMap EcsMap;
    Game::IScriptVM IscriptVM;
    Game::Trigger::SCRTriggerData ScrTriggerData;
    Game::BWAPI::BWAPIInterface BwapiInterface;
    Game::Replay::ReplayWriter ReplayWriter;
    Game::Networking::LockstepSync NetSync;
    Game::Networking::SyncTransport NetTransport;
    Game::Networking::RollbackStateManager RollbackMgr;
    Core::ECS::KinematicsSoAStream KinematicsStream;
    bool RollbackRecordingEnabled = false;
    size_t LastCreepSyncChangedTiles = 0;
    Game::Systems::FogOfWarSystem* FogSystem = nullptr;
    uint32_t ReplayFrame = 0;
    uint64_t SimulationTick = 0;
    double SimulationAccumulatorSeconds = 0.0;
    std::vector<Game::Replay::ReplayAction> ReplayActions;
    bool EcsInitialized = false;
    bool TerranAuditSpawned = false;
    bool MapLoaded = false;
    bool ReplayRecording = false;
    bool ReplayPlaying = false;
    bool NetSyncEnabled = false;
    bool IsHost = false;
    MatchOutcome Outcome = MatchOutcome::Playing;
    float MatchElapsedSeconds = 0.0f;
    bool MeleeVictoryCheckEnabled = true;
    std::array<bool, 8> ParticipatingPlayers{};

    MatchOutcome GetMatchOutcome() const { return Outcome; }
    void SetMatchOutcome(MatchOutcome NewOutcome) { Outcome = NewOutcome; }
    void ResetMatchOutcome()
    {
        Outcome = MatchOutcome::Playing;
        MatchElapsedSeconds = 0.0f;
        ParticipatingPlayers.fill(false);
    }
    float GetMatchElapsedSeconds() const { return MatchElapsedSeconds; }
    void AdvanceMatchElapsed(float Dt) { MatchElapsedSeconds += Dt; }
    void SetMeleeVictoryCheckEnabled(bool Enabled) { MeleeVictoryCheckEnabled = Enabled; }
    bool IsMeleeVictoryCheckEnabled() const { return MeleeVictoryCheckEnabled; }
    bool IsReplayPlaying() const { return ReplayPlaying; }
    bool IsMatchOver() const { return Outcome != MatchOutcome::Playing; }
    bool IsVictory() const { return Outcome == MatchOutcome::Victory; }
    bool IsDefeat() const { return Outcome == MatchOutcome::Defeat; }
    bool IsDraw() const { return Outcome == MatchOutcome::Draw; }
};

} // namespace Game::Client::GameLoop
