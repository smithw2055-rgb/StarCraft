#pragma once

#include <algorithm>
#include <memory>
#include <string>

#include "Core/Audio/AudioSystem.h"
#include "Core/Utils/Logger.h"
#include "Game/AI/AIBrain.h"
#include "Game/Components.h"
#include "Game/Races/RaceDef.h"
#include "Game/Systems/AnimationSystem.h"
#include "Game/Systems/BuildingSystem.h"
#include "Game/Systems/BulletSystem.h"
#include "Game/Systems/CombatSystem.h"
#include "Game/Systems/CommandSystem.h"
#include "Game/Systems/FogOfWarSystem.h"
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

inline StartRace RaceFromScriptPlayer(const GameSession& Session, int PlayerId)
{
    if (PlayerId < 0 || PlayerId >= (int)Session.GetScriptData().Players.size())
        return StartRace::Terran;

    return RaceFromScriptRace(Session.GetScriptData().Players[(size_t)PlayerId].Race);
}

inline StartRace RaceFromChkUnitID(int ChkUnitID, StartRace Fallback)
{
    const int DefID = Game::Races::GetUnitDefIDByChkUnitID(ChkUnitID);
    if (DefID < 0)
        return Fallback;

    const auto Def = Game::Races::GetUnitDefByID(DefID);
    if (Def.RaceType == Game::Components::Race::Zerg)
        return StartRace::Zerg;
    if (Def.RaceType == Game::Components::Race::Protoss)
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
    SysMgr.RegisterSystem(std::make_unique<Game::Systems::CombatSystem>(SysMgr, *BulletSysRaw));

    SysMgr.RegisterSystem(std::make_unique<Game::Systems::UpgradeSystem>(SysMgr));
    auto FogSys = std::make_unique<Game::Systems::FogOfWarSystem>(SysMgr, &Session.MutableEcsMap());
    Session.SetFogSystem(FogSys.get());
    SysMgr.RegisterSystem(std::move(FogSys));
    SysMgr.RegisterSystem(std::make_unique<Game::Systems::ZergSystem>(SysMgr, &Session.MutableEcsMap()));
    SysMgr.RegisterSystem(std::make_unique<Game::Systems::ProtossSystem>(SysMgr, &Session.MutableEcsMap()));
    SysMgr.RegisterSystem(std::make_unique<Game::Systems::NukeAndSpiderMineSystem>(SysMgr, *BulletSysRaw));
    SysMgr.RegisterSystem(std::make_unique<Game::Systems::SpellSystem>(SysMgr));
    SysMgr.RegisterSystem(std::make_unique<Game::Systems::AnimationSystem>(SysMgr, &Audio, BulletSysRaw));
    SysMgr.RegisterSystem(std::make_unique<Game::Systems::TransportSystem>(SysMgr));

    Game::Systems::CommandSystem::SetMap(&Session.MutableEcsMap());
    Game::Systems::BuildingSystem::SetMap(&Session.MutableEcsMap());
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
    int WorkerCount = 0;
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
        }
        if (Owner == PlayerId &&
            !EM.GetComponent<Game::Components::BuildingComponent>(EntityId) &&
            !EM.GetComponent<Game::Components::ResourceComponent>(EntityId) &&
            Detail::IsWorkerForRace(Race, Unit->ChkUnitID))
        {
            ++WorkerCount;
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
            Game::Systems::BuildingSystem::DebugSpawnCompletedBuilding(
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
            " at tile " + std::to_string(ChosenStart->TileX) + "," + std::to_string(ChosenStart->TileY));
    }
}

inline void RebuildPlayerSupplyAndRegisterAi(GameSession& Session)
{
    auto& SysMgr = Session.Systems();
    auto& EM = SysMgr.GetEntityManager();

    for (int PlayerId = 0; PlayerId < 8; ++PlayerId)
    {
        auto& Resources = Game::Systems::HarvestSystem::GetResources(PlayerId);
        Resources.SupplyUsed = 0;
        Resources.SupplyMax = 0;
    }

    bool HasPlayer1 = false;
    for (int Eid : EM.GetEntitiesWith<Game::Components::UnitComponent,
                                      Game::Components::PlayerComponent>())
    {
        auto* Unit = EM.GetComponent<Game::Components::UnitComponent>(Eid);
        auto* Player = EM.GetComponent<Game::Components::PlayerComponent>(Eid);
        if (!Unit || !Player)
            continue;
        if (Player->PlayerID == 1)
            HasPlayer1 = true;
        const int UnitDefID = Game::Races::GetUnitDefIDByChkUnitID(Unit->ChkUnitID);
        if (UnitDefID >= 0 && Player->PlayerID >= 0 && Player->PlayerID < 8)
        {
            const auto Def = Game::Races::GetUnitDefByID(UnitDefID);
            auto& Resources = Game::Systems::HarvestSystem::GetResources(Player->PlayerID);
            if (Def.IsBuilding)
                Resources.SupplyMax += Def.SupplyCost;
            else
                Resources.SupplyUsed += Def.SupplyCost;
        }
    }
    if (HasPlayer1)
        SysMgr.RegisterSystem(std::make_unique<Game::Systems::AIBrain>(SysMgr, 1, 0));
}

} // namespace Game::Client::GameLoop::GameSessionBootstrap
