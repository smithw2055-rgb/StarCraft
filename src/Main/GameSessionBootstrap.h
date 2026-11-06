#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "Core/Audio/AudioSystem.h"
#include "Core/Utils/Logger.h"
#include "Game/AI/AIDifficulty.h"
#include "Game/AI/AIBrain.h"
#include "Game/Components.h"
#include "Game/SimulationContext.h"
#include "Game/Races/RaceDef.h"
#include "Game/Races/UnitPrototypeRegistry.h"
#include "Game/Systems/AnimationSystem.h"
#include "Game/Systems/BuildingSystem.h"
#include "Game/Systems/BulletSystem.h"
#include "Game/Systems/CombatSystem.h"
#include "Game/Systems/CommandSystem.h"
#include "Game/Systems/FogOfWarSystem.h"
#include "Game/Systems/GeyserState.h"
#include "Game/Systems/HarvestSystem.h"
#include "Game/Systems/MovementSystem.h"
#include "Game/Systems/NukeAndSpiderMineSystem.h"
#include "Game/Systems/ProductionSystem.h"
#include "Game/Systems/ProtossSystem.h"
#include "Game/Systems/SpellSystem.h"
#include "Game/Systems/TransportSystem.h"
#include "Game/Systems/UpgradeSystem.h"
#include "Game/Systems/ZergSystem.h"
#include "Main/GameSession.h"

namespace Game::Client::GameLoop::GameSessionBootstrap
{
namespace Detail
{
enum class StartRace
{
    Terran,
    Zerg,
    Protoss
};

inline StartRace RaceFromScriptRace(uint8_t Race)
{
    switch (Race)
    {
    case 0: return StartRace::Zerg;
    case 2: return StartRace::Protoss;
    default: return StartRace::Terran;
    }
}

inline StartRace ApplyShowcaseRaceOverride(StartRace Fallback)
{
    const char* Value = std::getenv("STARCRAFT_SHOWCASE_RACE");
    if (!Value || Value[0] == '\0')
        return Fallback;
    std::string RaceName(Value);
    std::transform(RaceName.begin(), RaceName.end(), RaceName.begin(),
        [](unsigned char C) { return (char)std::tolower(C); });
    if (RaceName == "protoss")
        return StartRace::Protoss;
    if (RaceName == "zerg")
        return StartRace::Zerg;
    if (RaceName == "terran")
        return StartRace::Terran;
    return Fallback;
}

inline StartRace ApplyObserverRaceList(const std::string& RaceList,
                                       int PlayerId,
                                       StartRace Fallback)
{
    if (PlayerId < 0 || RaceList.empty())
        return Fallback;
    size_t Begin = 0;
    for (int Index = 0;; ++Index)
    {
        const size_t End = RaceList.find(',', Begin);
        if (Index == PlayerId)
        {
            std::string RaceName = RaceList.substr(
                Begin, End == std::string::npos ? std::string::npos : End - Begin);
            RaceName.erase(
                std::remove_if(RaceName.begin(), RaceName.end(),
                    [](unsigned char C) { return std::isspace(C) != 0; }),
                RaceName.end());
            std::transform(RaceName.begin(), RaceName.end(), RaceName.begin(),
                [](unsigned char C) { return (char)std::tolower(C); });
            if (RaceName == "zerg") return StartRace::Zerg;
            if (RaceName == "protoss") return StartRace::Protoss;
            if (RaceName == "terran") return StartRace::Terran;
            return Fallback;
        }
        if (End == std::string::npos)
            return Fallback;
        Begin = End + 1u;
    }
}

inline StartRace ApplyObserverRaceOverride(int PlayerId, StartRace Fallback)
{
    const char* Value = std::getenv("STARCRAFT_AI_OBSERVER_RACES");
    return Value && Value[0] != '\0'
        ? ApplyObserverRaceList(Value, PlayerId, Fallback)
        : Fallback;
}

inline const char* StartRaceName(StartRace Race)
{
    switch (Race)
    {
        case StartRace::Zerg: return "Zerg";
        case StartRace::Protoss: return "Protoss";
        default: return "Terran";
    }
}

inline Game::Systems::AIDifficulty GlobalAIDifficulty()
{
    const char* Value = std::getenv("STARCRAFT_AI_DIFFICULTY");
    return Game::Systems::ParseAIDifficulty(
        Value ? std::string_view(Value) : std::string_view(),
        Game::Systems::AIDifficulty::Medium);
}

inline Game::Systems::AIDifficulty ApplyPlayerDifficultyList(
    const std::string& DifficultyList,
    int PlayerId,
    Game::Systems::AIDifficulty Fallback)
{
    if (PlayerId < 0 || DifficultyList.empty())
        return Fallback;

    size_t Begin = 0;
    for (int Index = 0;; ++Index)
    {
        const size_t End = DifficultyList.find(',', Begin);
        if (Index == PlayerId)
        {
            const std::string_view Item(
                DifficultyList.data() + Begin,
                (End == std::string::npos ? DifficultyList.size() : End) - Begin);
            return Game::Systems::ParseAIDifficulty(Item, Fallback);
        }
        if (End == std::string::npos)
            return Fallback;
        Begin = End + 1u;
    }
}

inline Game::Systems::AIDifficulty AIDifficultyForPlayer(int PlayerId)
{
    const auto Fallback = GlobalAIDifficulty();
    const char* Value = std::getenv("STARCRAFT_AI_DIFFICULTIES");
    return Value && Value[0] != '\0'
        ? ApplyPlayerDifficultyList(Value, PlayerId, Fallback)
        : Fallback;
}

inline size_t ScriptPlayerDataIndex(const GameSession& Session, int PlayerId)
{
    if (PlayerId >= 0 && PlayerId < (int)Session.GetScriptData().Players.size())
        return (size_t)PlayerId;
    return 0u;
}

inline bool IsForcedSkirmishAiPlayer(int PlayerId)
{
    const char* Value = std::getenv("STARCRAFT_SKIRMISH_AI_PLAYERS");
    if (!Value || Value[0] == '\0' || PlayerId < 0)
        return false;

    std::string List(Value);
    size_t Begin = 0;
    while (Begin < List.size())
    {
        const size_t End = List.find(',', Begin);
        const std::string Item = List.substr(
            Begin, End == std::string::npos ? std::string::npos : End - Begin);
        char* ParsedEnd = nullptr;
        const long Parsed = std::strtol(Item.c_str(), &ParsedEnd, 10);
        if (ParsedEnd != Item.c_str() && Parsed == PlayerId)
            return true;
        if (End == std::string::npos)
            break;
        Begin = End + 1u;
    }
    return false;
}

inline bool ShouldEnsurePlayableStart(const GameSession& Session, int PlayerId)
{
    if (IsForcedSkirmishAiPlayer(PlayerId))
        return true;
    const char* ShowcaseValue = std::getenv("STARCRAFT_AI_SHOWCASE");
    if (ShowcaseValue && ShowcaseValue[0] != '\0' && ShowcaseValue[0] != '0')
    {
        // A campaign observation/rescue slot may not own an economy.  The
        // showcase supplies only the standard melee starting state; every
        // later resource, building and unit still goes through gameplay.
        return PlayerId == Session.GetLocalPlayerID();
    }
    const auto& Data = Session.GetScriptData();
    if (Data.RawData.empty())
        return true;

    const uint8_t Owner = Data.Players[ScriptPlayerDataIndex(Session, PlayerId)].Owner;
    return Owner == 5 || Owner == 6; // Computer or Human; skip Rescue Passive and Neutral slots.
}

inline bool IsComputerPlayer(const GameSession& Session, int PlayerId)
{
    if (IsForcedSkirmishAiPlayer(PlayerId))
        return true;
    const auto& Data = Session.GetScriptData();
    if (Data.RawData.empty())
        return PlayerId > 0;
    return Data.Players[ScriptPlayerDataIndex(Session, PlayerId)].Owner == 5;
}

inline bool IsAiObserverMatch()
{
    const char* Value = std::getenv("STARCRAFT_AI_OBSERVER_MATCH");
    return Value && Value[0] != '\0' && Value[0] != '0';
}

inline bool IsAiObserverRevealToAi()
{
    const char* Value = std::getenv("STARCRAFT_AI_OBSERVER_REVEAL_TO_AI");
    return Value && Value[0] != '\0' && Value[0] != '0';
}

inline bool IsCampaignMissionPath(std::string MapPath)
{
    std::replace(MapPath.begin(), MapPath.end(), '\\', '/');
    std::transform(MapPath.begin(), MapPath.end(), MapPath.begin(),
        [](unsigned char C) { return (char)std::tolower(C); });
    return MapPath.find("/campaign/") != std::string::npos;
}

inline StartRace RaceFromScriptPlayer(const GameSession& Session, int PlayerId)
{
    if (PlayerId < 0 || PlayerId >= (int)Session.GetScriptData().Players.size())
        return StartRace::Terran;

    return RaceFromScriptRace(
        Session.GetScriptData().Players[ScriptPlayerDataIndex(Session, PlayerId)].Race);
}

inline StartRace RaceFromChkUnitID(int ChkUnitID, StartRace Fallback)
{
    Game::Components::Race FallbackRace = Game::Components::Race::Terran;
    if (Fallback == StartRace::Zerg)
        FallbackRace = Game::Components::Race::Zerg;
    else if (Fallback == StartRace::Protoss)
        FallbackRace = Game::Components::Race::Protoss;

    const auto Race = Game::Races::GetRaceByChkUnitID(ChkUnitID, FallbackRace);
    if (Race == Game::Components::Race::Zerg)
        return StartRace::Zerg;
    if (Race == Game::Components::Race::Protoss)
        return StartRace::Protoss;
    return StartRace::Terran;
}

inline bool IsTownHallForRace(StartRace Race, int ChkUnitID)
{
    const int DefID = Game::Races::GetUnitDefIDByChkUnitID(ChkUnitID);
    if (DefID < 0)
        return false;
    const auto Def = Game::Races::GetUnitDefByID(DefID);
    if (!Def.IsBuilding)
        return false;

    if (Race == StartRace::Zerg)
        return Def.Name == "Hatchery" || Def.Name == "Lair" || Def.Name == "Hive";
    if (Race == StartRace::Protoss)
        return Def.Name == "Nexus";
    return Def.Name == "CommandCenter";
}

inline bool IsWorkerForRace(StartRace Race, int ChkUnitID)
{
    const int DefID = Game::Races::GetUnitDefIDByChkUnitID(ChkUnitID);
    if (DefID < 0)
        return false;
    const auto Def = Game::Races::GetUnitDefByID(DefID);

    if (Race == StartRace::Zerg)
        return Def.Name == "Drone";
    if (Race == StartRace::Protoss)
        return Def.Name == "Probe";
    return Def.Name == "SCV";
}

inline int TownHallDefForRace(StartRace Race)
{
    if (Race == StartRace::Zerg)
        return Game::Races::GetUnitDefID("Hatchery");
    if (Race == StartRace::Protoss)
        return Game::Races::GetUnitDefID("Nexus");
    return Game::Races::GetUnitDefID("CommandCenter");
}

inline int WorkerDefForRace(StartRace Race)
{
    if (Race == StartRace::Zerg)
        return Game::Races::GetUnitDefID("Drone");
    if (Race == StartRace::Protoss)
        return Game::Races::GetUnitDefID("Probe");
    return Game::Races::GetUnitDefID("SCV");
}
}

class AiObserverMatchMonitor final : public Core::ECS::ISystem
{
public:
    AiObserverMatchMonitor(Core::ECS::SystemManager& SysMgr,
                           std::vector<int> Participants)
        : mSysMgr(SysMgr), mParticipants(std::move(Participants))
    {
        for (int PlayerID : mParticipants)
            if (PlayerID >= 0 && PlayerID < 8)
                mAlive[(size_t)PlayerID] = true;
    }

    std::string_view GetName() const override { return "AiObserverMatchMonitor"; }

    void Update(float DeltaTime) override
    {
        if (mFinished || DeltaTime <= 0.0f)
            return;
        mElapsed += DeltaTime;
        mCheckTimer += DeltaTime;
        mSnapshotTimer += DeltaTime;
        if (mCheckTimer < 2.0f)
            return;
        mCheckTimer = 0.0f;

        std::array<int, 8> UnitCounts{};
        std::array<int, 8> BuildingCounts{};
        std::array<int, 8> CombatCounts{};
        auto& EM = mSysMgr.GetEntityManager();
        for (int Eid : EM.GetEntitiesWith<Game::Components::UnitComponent,
                                          Game::Components::PlayerComponent>())
        {
            auto* Unit = EM.GetComponent<Game::Components::UnitComponent>(Eid);
            auto* Player = EM.GetComponent<Game::Components::PlayerComponent>(Eid);
            if (!Unit || !Player || Unit->HitPoints <= 0 ||
                Player->PlayerID < 0 || Player->PlayerID >= 8)
            {
                continue;
            }
            ++UnitCounts[(size_t)Player->PlayerID];
            // Unit eggs reuse BuildingComponent for morph progress but do not
            // satisfy the melee "destroy all structures" victory condition.
            if (Game::Systems::IsMeleeStructureEntity(EM, Eid))
                ++BuildingCounts[(size_t)Player->PlayerID];
            auto* Combat = EM.GetComponent<Game::Components::CombatComponent>(Eid);
            if (Combat && (Combat->GroundDamage > 0 || Combat->AirDamage > 0))
                ++CombatCounts[(size_t)Player->PlayerID];
        }

        int AliveCount = 0;
        int Winner = -1;
        for (int PlayerID : mParticipants)
        {
            const bool AliveNow = BuildingCounts[(size_t)PlayerID] > 0;
            if (mElapsed >= 10.0f && mAlive[(size_t)PlayerID] && !AliveNow)
            {
                mAlive[(size_t)PlayerID] = false;
                Core::Utils::Logger::Info(
                    "AiObserverMatch", "player " + std::to_string(PlayerID) +
                    " eliminated at simulated_seconds=" + std::to_string((int)mElapsed));
            }
            if (mAlive[(size_t)PlayerID] && AliveNow)
            {
                ++AliveCount;
                Winner = PlayerID;
            }
        }

        if (mSnapshotTimer >= 30.0f)
        {
            mSnapshotTimer = 0.0f;
            std::string Snapshot = "status simulated_seconds=" + std::to_string((int)mElapsed);
            for (int PlayerID : mParticipants)
            {
                const auto& Resources = Game::Systems::HarvestSystem::GetResources(EM, PlayerID);
                Snapshot += " p" + std::to_string(PlayerID) + "{units=" +
                    std::to_string(UnitCounts[(size_t)PlayerID]) + ",buildings=" +
                    std::to_string(BuildingCounts[(size_t)PlayerID]) + ",combat=" +
                    std::to_string(CombatCounts[(size_t)PlayerID]) + ",minerals=" +
                    std::to_string(Resources.Minerals) + ",gas=" +
                    std::to_string(Resources.VespeneGas) + "}";
            }
            Core::Utils::Logger::Info("AiObserverMatch", Snapshot);
        }

        if (mElapsed >= 10.0f && AliveCount <= 1)
        {
            mFinished = true;
            if (Winner >= 0)
                Core::Utils::Logger::Info(
                    "AiObserverMatch", "winner player=" + std::to_string(Winner) +
                    " simulated_seconds=" + std::to_string((int)mElapsed));
            else
                Core::Utils::Logger::Info(
                    "AiObserverMatch", "draw simulated_seconds=" +
                    std::to_string((int)mElapsed));
        }
    }

private:
    Core::ECS::SystemManager& mSysMgr;
    std::vector<int> mParticipants;
    std::array<bool, 8> mAlive{};
    float mElapsed = 0.0f;
    float mCheckTimer = 0.0f;
    float mSnapshotTimer = 0.0f;
    bool mFinished = false;
};

inline void RegisterGameplaySystems(GameSession& Session, Core::Audio::AudioSystem& Audio)
{
    auto& SysMgr = Session.Systems();
    auto CmdSys = std::make_unique<Game::Systems::CommandSystem>();
    CmdSys->Init(&SysMgr);
    SysMgr.RegisterSystem(std::move(CmdSys));
    SysMgr.RegisterSystem(std::make_unique<Game::Systems::MovementSystem>(SysMgr, &Session.MutableEcsMap()));
    SysMgr.RegisterSystem(std::make_unique<Game::Systems::HarvestSystem>(SysMgr));
    SysMgr.RegisterSystem(std::make_unique<Game::Systems::BuildingSystem>(SysMgr));
    SysMgr.RegisterSystem(std::make_unique<Game::Systems::ProductionSystem>(SysMgr));

    auto BulletSys = std::make_unique<Game::Systems::BulletSystem>(SysMgr);
    auto* BulletSysRaw = BulletSys.get();
    SysMgr.RegisterSystem(std::move(BulletSys));
    SysMgr.RegisterSystem(std::make_unique<Game::Systems::CombatSystem>(
        SysMgr, *BulletSysRaw, &Session.MutableEcsMap()));

    SysMgr.RegisterSystem(std::make_unique<Game::Systems::UpgradeSystem>(SysMgr));
    auto FogSys = std::make_unique<Game::Systems::FogOfWarSystem>(SysMgr, &Session.MutableEcsMap());
    FogSys->SetLocalPlayerID(Session.GetLocalPlayerID());
    FogSys->SetObserverRevealAll(Detail::IsAiObserverMatch());
    Session.SetFogSystem(FogSys.get());
    SysMgr.RegisterSystem(std::move(FogSys));
    SysMgr.RegisterSystem(std::make_unique<Game::Systems::ZergSystem>(SysMgr, &Session.MutableEcsMap()));
    SysMgr.RegisterSystem(std::make_unique<Game::Systems::ProtossSystem>(SysMgr, &Session.MutableEcsMap()));
    SysMgr.RegisterSystem(std::make_unique<Game::Systems::NukeAndSpiderMineSystem>(SysMgr, *BulletSysRaw));
    SysMgr.RegisterSystem(std::make_unique<Game::Systems::SpellSystem>(SysMgr));
    SysMgr.RegisterSystem(std::make_unique<Game::Systems::AnimationSystem>(SysMgr, &Audio, BulletSysRaw));
    SysMgr.RegisterSystem(std::make_unique<Game::Systems::TransportSystem>(SysMgr));

    auto& SimCtx = Game::SimulationContext::Get(Session.Entities());
    SimCtx.Map = &Session.GetEcsMap();
    SimCtx.MutableMap = &Session.MutableEcsMap();
    SimCtx.FogOfWar = Session.GetFogSystem();
    SimCtx.LocalPlayerID = Session.GetLocalPlayerID();

    Game::Systems::CommandSystem::SetMap(&Session.MutableEcsMap());
    Game::Systems::BuildingSystem::ResetPendingBuildOrders();
    Game::Systems::BuildingSystem::SetMap(&Session.MutableEcsMap());
    Game::Systems::BuildingSystem::SetFogOfWarSystem(Session.GetFogSystem());
    Game::Systems::CombatSystem::SetFogOfWarSystem(Session.GetFogSystem());
    Game::Systems::ProductionSystem::SetMap(&Session.MutableEcsMap());
    Game::Systems::TransportSystem::SetMap(&Session.MutableEcsMap());
}

template <typename SpawnResourceEntityT, typename IsMineralUnitT, typename IsGeyserUnitT>
inline void EnsurePlayableStart(GameSession& Session,
                                int PlayerId,
                                SpawnResourceEntityT&& SpawnResourceEntity,
                                IsMineralUnitT&& IsMineralUnit,
                                IsGeyserUnitT&& IsGeyserUnit)
{
    (void)IsMineralUnit;
    (void)IsGeyserUnit;

    if (!Detail::ShouldEnsurePlayableStart(Session, PlayerId))
        return;

    const Game::Client::StartLocation* ChosenStart = nullptr;
    for (const auto& StartLocation : Session.GetMapData().StartLocations)
    {
        if (StartLocation.PlayerId == PlayerId)
        {
            ChosenStart = &StartLocation;
            break;
        }
    }
    if (!Session.GetMapData().StartLocations.empty() && !ChosenStart)
    {
        ChosenStart = &Session.GetMapData().StartLocations[
            std::min<size_t>((size_t)PlayerId, Session.GetMapData().StartLocations.size() - 1)];
    }
    if (!ChosenStart)
        return;

    const int BaseX = ChosenStart->TileX * 32 + 16;
    const int BaseY = ChosenStart->TileY * 32 + 16;
    const int ResourceRadius = 448;
    const int ResourceRadiusSq = ResourceRadius * ResourceRadius;

    auto IsNearStart = [&](const Core::Math::Vec2& Pos)
    {
        const int DX = (int)Pos.X - BaseX;
        const int DY = (int)Pos.Y - BaseY;
        return DX * DX + DY * DY <= ResourceRadiusSq;
    };

    bool HasTownHall = false;
    int TownHallEntityId = -1;
    int WorkerCount = 0;
    int OverlordCount = 0;
    int LarvaCount = 0;
    Detail::StartRace Race = Detail::RaceFromScriptPlayer(Session, PlayerId);
    bool HasNearbyMinerals = false;
    bool HasNearbyGas = false;
    auto Existing = Session.Entities().GetEntitiesWith<
        Game::Components::TransformComponent,
        Game::Components::UnitComponent>();
    for (int EntityId : Existing)
    {
        auto& EM = Session.Entities();
        auto* Trans = EM.GetComponent<Game::Components::TransformComponent>(EntityId);
        auto* Unit = EM.GetComponent<Game::Components::UnitComponent>(EntityId);
        auto* Player = EM.GetComponent<Game::Components::PlayerComponent>(EntityId);
        if (!Trans || !Unit)
            continue;
        const int Owner = Player ? Player->PlayerID : 11;
        if (Owner == PlayerId && !EM.GetComponent<Game::Components::ResourceComponent>(EntityId))
            Race = Detail::RaceFromChkUnitID(Unit->ChkUnitID, Race);
    }
    Race = Detail::ApplyShowcaseRaceOverride(Race);
    Race = Detail::ApplyObserverRaceOverride(PlayerId, Race);

    for (int EntityId : Existing)
    {
        auto& EM = Session.Entities();
        auto* Trans = EM.GetComponent<Game::Components::TransformComponent>(EntityId);
        auto* Unit = EM.GetComponent<Game::Components::UnitComponent>(EntityId);
        auto* Player = EM.GetComponent<Game::Components::PlayerComponent>(EntityId);
        if (!Trans || !Unit)
            continue;
        const int Owner = Player ? Player->PlayerID : 11;
        if (Owner == PlayerId &&
            EM.GetComponent<Game::Components::BuildingComponent>(EntityId) &&
            Detail::IsTownHallForRace(Race, Unit->ChkUnitID))
        {
            HasTownHall = true;
            TownHallEntityId = EntityId;
        }
        if (Owner == PlayerId &&
            !EM.GetComponent<Game::Components::BuildingComponent>(EntityId) &&
            !EM.GetComponent<Game::Components::ResourceComponent>(EntityId) &&
            Detail::IsWorkerForRace(Race, Unit->ChkUnitID))
        {
            ++WorkerCount;
        }
        if (Owner == PlayerId && Game::Races::IsOverlordChkUnitID(Unit->ChkUnitID))
        {
            ++OverlordCount;
        }
        if (Owner == PlayerId && Game::Races::IsLarvaChkUnitID(Unit->ChkUnitID))
        {
            ++LarvaCount;
        }
        auto* Resource = EM.GetComponent<Game::Components::ResourceComponent>(EntityId);
        if (Resource && IsNearStart(Trans->Position))
        {
            if (Resource->Type == Game::Components::ResourceType::Minerals) HasNearbyMinerals = true;
            if (Resource->Type == Game::Components::ResourceType::VespeneGas) HasNearbyGas = true;
        }
    }

    bool SpawnedAny = false;

    if (!HasTownHall)
    {
        const int TownHallDefID = Detail::TownHallDefForRace(Race);
        if (TownHallDefID >= 0)
            TownHallEntityId = Game::Systems::BuildingSystem::DebugSpawnCompletedBuilding(
                Session.Entities(), TownHallDefID, {(float)BaseX, (float)BaseY}, PlayerId);
        SpawnedAny = true;
    }

    for (int I = WorkerCount; I < 4; ++I)
    {
        const int WorkerDefID = Detail::WorkerDefForRace(Race);
        if (WorkerDefID >= 0)
            Game::Systems::ProductionSystem::DebugSpawnUnit(
                Session.Entities(), WorkerDefID,
                {(float)(BaseX - 36 + I * 24), (float)(BaseY + 64)}, PlayerId);
        SpawnedAny = true;
    }

    if (Race == Detail::StartRace::Zerg)
    {
        if (OverlordCount == 0)
        {
            const int OverlordDefID = Game::Races::GetUnitDefIDByChkUnitID(42);
            if (OverlordDefID >= 0)
            {
                Game::Systems::ProductionSystem::DebugSpawnUnit(
                    Session.Entities(), OverlordDefID,
                    {(float)(BaseX + 64), (float)(BaseY - 64)}, PlayerId);
                SpawnedAny = true;
            }
        }

        if (LarvaCount < 3)
        {
            const int LarvaDefID = Game::Races::GetUnitDefIDByChkUnitID(35);
            if (LarvaDefID >= 0)
            {
                for (int I = LarvaCount; I < 3; ++I)
                {
                    const int LarvaEid = Game::Systems::ProductionSystem::DebugSpawnUnit(
                        Session.Entities(), LarvaDefID,
                        {(float)(BaseX - 32 + I * 24), (float)(BaseY + 48)}, PlayerId);
                    if (LarvaEid >= 0)
                    {
                        auto* LarvaComp = Session.Entities().GetComponent<Game::Components::LarvaComponent>(LarvaEid);
                        auto& LLarva = LarvaComp ? *LarvaComp : Session.Entities().AddComponent<Game::Components::LarvaComponent>(LarvaEid);
                        LLarva.ParentHatcheryEntityID = TownHallEntityId;
                    }
                    SpawnedAny = true;
                }
            }
        }
    }

    const int Facing = PlayerId == 0 ? -1 : 1;
    if (!HasNearbyMinerals)
    {
        const int MineralOffsets[8][2] =
        {
            {-128, -96}, {-96, -128}, {-48, -144}, {0, -144},
            {48, -128}, {96, -96}, {128, -48}, {128, 8}
        };
        for (const auto& Offset : MineralOffsets)
            SpawnResourceEntity(100, BaseX + Offset[0] * Facing, BaseY + Offset[1] * Facing, 1500);
        SpawnedAny = true;
    }
    if (!HasNearbyGas)
    {
        SpawnResourceEntity(188, BaseX + 176 * Facing, BaseY - 32 * Facing, 5000);
        SpawnedAny = true;
    }

    if (SpawnedAny)
    {
        Core::Utils::Logger::Info("GameController",
            "Ensured playable start for player " + std::to_string(PlayerId) +
            " race=" + Detail::StartRaceName(Race) +
            " at tile " + std::to_string(ChosenStart->TileX) + "," +
            std::to_string(ChosenStart->TileY));
    }
}

inline void RebuildPlayerSupplyAndRegisterAi(GameSession& Session,
                                             bool RegisterGenericAi)
{
    auto& SysMgr = Session.Systems();
    auto& EM = SysMgr.GetEntityManager();

    for (int PlayerId = 0; PlayerId < 8; ++PlayerId)
    {
        auto& Resources = Game::Systems::HarvestSystem::GetResources(EM, PlayerId);
        Resources.SupplyUsed = 0;
        Resources.SupplyMax = 0;
    }

    bool HasPlayerUnits[8] = {};
    for (int Eid : EM.GetEntitiesWith<Game::Components::UnitComponent,
                                      Game::Components::PlayerComponent>())
    {
        auto* Unit = EM.GetComponent<Game::Components::UnitComponent>(Eid);
        auto* Player = EM.GetComponent<Game::Components::PlayerComponent>(Eid);
        if (!Unit || !Player)
            continue;
        if (Player->PlayerID >= 0 && Player->PlayerID < 8)
            HasPlayerUnits[Player->PlayerID] = true;
        const int UnitDefID = Game::Races::GetUnitDefIDByChkUnitID(Unit->ChkUnitID);
        if (UnitDefID >= 0 && Player->PlayerID >= 0 && Player->PlayerID < 8)
        {
            const auto Def = Game::Races::GetUnitDefByID(UnitDefID);
            auto& Resources = Game::Systems::HarvestSystem::GetResources(EM, Player->PlayerID);
            const int SupplyProvided = Game::Races::GetSupplyProvidedByDefID(UnitDefID);
            if (SupplyProvided > 0)
                Resources.SupplyMax += SupplyProvided;
            if (!Def.IsBuilding)
                Resources.SupplyUsed += Def.SupplyCost;
        }
    }
    const char* ShowcaseValue = std::getenv("STARCRAFT_AI_SHOWCASE");
    const bool RunShowcase = ShowcaseValue && ShowcaseValue[0] != '\0' &&
                             ShowcaseValue[0] != '0';
    if (RunShowcase)
    {
        const int ShowcasePlayerId = Session.GetLocalPlayerID();
        const auto Difficulty = Detail::AIDifficultyForPlayer(ShowcasePlayerId);
        SysMgr.RegisterSystem(std::make_unique<Game::Systems::AIBrain>(
            SysMgr, ShowcasePlayerId, ShowcasePlayerId, true,
            &Session.MutableEcsMap(), Difficulty, Session.GetFogSystem()));
        Core::Utils::Logger::Info(
            "GameController",
            "Registered real-progression showcase AIBrain for local player " +
                std::to_string(ShowcasePlayerId) + " difficulty=" +
                std::string(Game::Systems::AIDifficultyName(Difficulty)));
    }

    if (!RegisterGenericAi)
    {
        // Campaign UNIT placements are controlled by CHK triggers and the
        // mission's AI scripts.  Attaching the generic skirmish brain here
        // selects every pre-placed combat unit and collapses the whole map into
        // one attack wave before the mission scripts can issue their orders.
        Core::Utils::Logger::Info(
            "GameController",
            "Generic skirmish AI disabled for campaign mission");
        return;
    }

    if (Detail::IsAiObserverMatch())
    {
        std::vector<int> Participants;
        for (int PlayerId = 0; PlayerId < 8; ++PlayerId)
            if (HasPlayerUnits[PlayerId])
                Participants.push_back(PlayerId);

        const bool RevealToAi = Detail::IsAiObserverRevealToAi();
        auto* AiFog = RevealToAi ? nullptr : Session.GetFogSystem();
        for (size_t I = 0; I < Participants.size(); ++I)
        {
            const int PlayerId = Participants[I];
            const int TargetPlayerId = Participants[(I + 1u) % Participants.size()];
            const auto Difficulty = Detail::AIDifficultyForPlayer(PlayerId);
            SysMgr.RegisterSystem(std::make_unique<Game::Systems::AIBrain>(
                SysMgr, PlayerId, TargetPlayerId, false,
                &Session.MutableEcsMap(), Difficulty, AiFog));
            Core::Utils::Logger::Info(
                "AiObserverMatch", "registered AIBrain player=" +
                std::to_string(PlayerId) + " initial_target=" +
                std::to_string(TargetPlayerId) + " difficulty=" +
                std::string(Game::Systems::AIDifficultyName(Difficulty)));
        }
        SysMgr.RegisterSystem(std::make_unique<AiObserverMatchMonitor>(
            SysMgr, Participants));
        Core::Utils::Logger::Info(
            "AiObserverMatch", "observer match started participants=" +
            std::to_string(Participants.size()) + " reveal_all=1 ai_reveal_all=" +
            std::to_string(RevealToAi ? 1 : 0));
        return;
    }

    const int TargetPlayerId = Session.GetLocalPlayerID();
    for (int PlayerId = 0; PlayerId < 8; ++PlayerId)
    {
        if (PlayerId == TargetPlayerId)
            continue;
        if (HasPlayerUnits[PlayerId] && Detail::IsComputerPlayer(Session, PlayerId))
        {
            const auto Difficulty = Detail::AIDifficultyForPlayer(PlayerId);
            SysMgr.RegisterSystem(std::make_unique<Game::Systems::AIBrain>(
                SysMgr, PlayerId, TargetPlayerId, false,
                &Session.MutableEcsMap(), Difficulty, Session.GetFogSystem()));
            Core::Utils::Logger::Info(
                "GameController", "Registered AIBrain player=" +
                std::to_string(PlayerId) + " target=" +
                std::to_string(TargetPlayerId) + " difficulty=" +
                std::string(Game::Systems::AIDifficultyName(Difficulty)));
        }
    }
}

} // namespace Game::Client::GameLoop::GameSessionBootstrap
