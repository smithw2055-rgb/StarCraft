#include "Game/AI/AIBrain.h"
#include "Game/Systems/HarvestSystem.h"
#include "Game/Systems/ProductionSystem.h"
#include "Game/Systems/BuildingSystem.h"
#include "Game/Systems/CommandSystem.h"
#include "Game/Systems/CombatSystem.h"
#include "Game/Systems/ZergSystem.h"
#include "Game/Components.h"
#include "Game/Races/RaceDef.h"
#include "Game/Map/Map.h"
#include "Core/Utils/Logger.h"

#include <algorithm>
#include <cstdlib>

namespace Game::Systems
{

using namespace Game::Components;

AIBrain::AIBrain(Core::ECS::SystemManager& SysMgr, int AIPlayerID, int TargetPlayerID)
    : mSystemMgr(SysMgr)
    , mAIPlayerID(AIPlayerID)
    , mTargetPlayerID(TargetPlayerID)
{
}

Core::Math::Vec2 AIBrain::FindBaseLocation(Core::ECS::EntityManager& EM)
{
    auto Entities = EM.GetEntitiesWith<TransformComponent, PlayerComponent, BuildingComponent>();
    for (int Eid : Entities)
    {
        auto* Player = EM.GetComponent<PlayerComponent>(Eid);
        auto* Trans = EM.GetComponent<TransformComponent>(Eid);
        if (Player && Player->PlayerID == mAIPlayerID && Trans)
            return Trans->Position;
    }
    return Core::Math::Vec2(1000.0f, 1000.0f);
}

Core::Math::Vec2 AIBrain::FindEnemyBaseLocation(Core::ECS::EntityManager& EM)
{
    auto Entities = EM.GetEntitiesWith<TransformComponent, PlayerComponent, BuildingComponent>();
    for (int Eid : Entities)
    {
        auto* Player = EM.GetComponent<PlayerComponent>(Eid);
        auto* Trans = EM.GetComponent<TransformComponent>(Eid);
        if (Player && Player->PlayerID == mTargetPlayerID && Trans)
            return Trans->Position;
    }
    return Core::Math::Vec2(2000.0f, 2000.0f);
}

Core::Math::Vec2 AIBrain::FindExpansionLocation(Core::ECS::EntityManager& EM)
{
    // Find a location away from base with minerals nearby
    auto Minerals = EM.GetEntitiesWith<TransformComponent, ResourceComponent>();
    for (int Mid : Minerals)
    {
        auto* Res = EM.GetComponent<ResourceComponent>(Mid);
        auto* Trans = EM.GetComponent<TransformComponent>(Mid);
        if (!Res || !Trans || Res->Type != ResourceType::Minerals || Res->IsDepleted) continue;

        float D = Trans->Position.DistanceTo(mAiBasePos);
        if (D > 300.0f && D < 800.0f)
            return Trans->Position;
    }
    return {mAiBasePos.X + 400.0f, mAiBasePos.Y};
}

int AIBrain::CountUnits(Core::ECS::EntityManager& EM, int PlayerID, int ChkUnitID)
{
    int Count = 0;
    auto Entities = EM.GetEntitiesWith<UnitComponent, PlayerComponent>();
    for (int Eid : Entities)
    {
        auto* Unit = EM.GetComponent<UnitComponent>(Eid);
        auto* Player = EM.GetComponent<PlayerComponent>(Eid);
        if (Unit && Player && Player->PlayerID == PlayerID &&
            (ChkUnitID < 0 || Unit->ChkUnitID == ChkUnitID))
            Count++;
    }
    return Count;
}

int AIBrain::CountBuildings(Core::ECS::EntityManager& EM, int PlayerID, bool ExcludeAddons)
{
    int Count = 0;
    auto Entities = EM.GetEntitiesWith<BuildingComponent, PlayerComponent>();
    for (int Eid : Entities)
    {
        auto* B = EM.GetComponent<BuildingComponent>(Eid);
        auto* P = EM.GetComponent<PlayerComponent>(Eid);
        if (B && P && P->PlayerID == PlayerID && B->IsBuilding && !B->UnderConstruction)
        {
            if (!ExcludeAddons || !B->IsAddon)
                Count++;
        }
    }
    return Count;
}

int AIBrain::CountByDefID(Core::ECS::EntityManager& EM, int PlayerID, int DefID)
{
    int Count = 0;
    auto Entities = EM.GetEntitiesWith<PlayerComponent>();
    for (int Eid : Entities)
    {
        auto* P = EM.GetComponent<PlayerComponent>(Eid);
        if (!P || P->PlayerID != PlayerID) continue;

        auto* Bldg = EM.GetComponent<BuildingComponent>(Eid);
        auto* Unit = EM.GetComponent<UnitComponent>(Eid);

        if (Bldg && Bldg->BuildingDefID == DefID && !Bldg->UnderConstruction) Count++;
        else if (Unit && Unit->HitPoints > 0 && Unit->ChkUnitID ==
                 Game::Races::GetUnitDefByID(DefID).ChkUnitID) Count++;
    }
    return Count;
}

int AIBrain::GetRace(Core::ECS::EntityManager& EM)
{
    auto Entities = EM.GetEntitiesWith<UnitComponent, PlayerComponent>();
    for (int Eid : Entities)
    {
        auto* U = EM.GetComponent<UnitComponent>(Eid);
        auto* P = EM.GetComponent<PlayerComponent>(Eid);
        if (U && P && P->PlayerID == mAIPlayerID && U->HitPoints > 0)
            return (int)U->UnitRace;
    }
    return 0; // Terran default
}

int AIBrain::FindProducerFor(Core::ECS::EntityManager& EM, int UnitDefID)
{
    const auto& Def = Game::Races::GetUnitDefByID(UnitDefID);
    if (Def.ProducedBy < 0) return -1;
    const auto& ProducerDef = Game::Races::GetUnitDefByID(Def.ProducedBy);

    auto Buildings = EM.GetEntitiesWith<BuildingComponent, PlayerComponent>();
    for (int Bid : Buildings)
    {
        auto* B = EM.GetComponent<BuildingComponent>(Bid);
        auto* P = EM.GetComponent<PlayerComponent>(Bid);
        if (B && P && P->PlayerID == mAIPlayerID &&
            B->BuildingDefID == Def.ProducedBy && !B->UnderConstruction)
            return Bid;
    }
    return -1;
}

int AIBrain::FindIdleWorker(Core::ECS::EntityManager& EM)
{
    int RaceVal = GetRace(EM);
    int WorkerChkIDs[] = {0x07, 0x29, 0x2D}; // SCV, Drone, Probe

    auto Units = EM.GetEntitiesWith<UnitComponent, PlayerComponent, HarvestComponent>();
    for (int Eid : Units)
    {
        auto* U = EM.GetComponent<UnitComponent>(Eid);
        auto* P = EM.GetComponent<PlayerComponent>(Eid);
        auto* H = EM.GetComponent<HarvestComponent>(Eid);
        if (!U || !P || !H) continue;
        if (P->PlayerID != mAIPlayerID) continue;
        if (U->ChkUnitID != WorkerChkIDs[RaceVal]) continue;
        if (H->State == HarvestState::Idle)
            return Eid;
    }
    return -1;
}

int AIBrain::FindNearestFreeBuilding(Core::ECS::EntityManager& EM, int BuildingDefID)
{
    int BestID = -1;
    float BestDist = 1e9f;
    auto Buildings = EM.GetEntitiesWith<BuildingComponent, PlayerComponent, TransformComponent>();
    for (int Bid : Buildings)
    {
        auto* B = EM.GetComponent<BuildingComponent>(Bid);
        auto* P = EM.GetComponent<PlayerComponent>(Bid);
        auto* T = EM.GetComponent<TransformComponent>(Bid);
        if (!B || !P || !T || P->PlayerID != mAIPlayerID) continue;
        if (B->BuildingDefID == BuildingDefID && !B->UnderConstruction)
        {
            int QSize = 0;
            auto* Prod = EM.GetComponent<ProductionComponent>(Bid);
            if (Prod) QSize = (int)Prod->Queue.size();
            if (QSize < 3)
            {
                float D = T->Position.DistanceTo(mAiBasePos);
                if (D < BestDist) { BestDist = D; BestID = Bid; }
            }
        }
    }
    return BestID;
}

void AIBrain::TrainWorkerIfNeeded(Core::ECS::EntityManager& EM,
                                   int ProducerDefID, int WorkerDefID, int MaxWorkers)
{
    int WorkerCount = CountByDefID(EM, mAIPlayerID, WorkerDefID);
    auto& Res = HarvestSystem::GetResources(mAIPlayerID);
    const auto& WorkerDef = Game::Races::GetUnitDefByID(WorkerDefID);

    if (WorkerCount < MaxWorkers && Res.Minerals >= WorkerDef.MineralCost)
    {
        int Producer = FindProducerFor(EM, WorkerDefID);
        if (Producer >= 0)
            ProductionSystem::StartTrain(EM, Producer, WorkerDefID);
    }
}

void AIBrain::RunTerranAI(Core::ECS::EntityManager& EM)
{
    int SvcDefID = Game::Races::GetUnitDefID("SCV");
    int MarineDefID = Game::Races::GetUnitDefID("Marine");
    int FirebatDefID = Game::Races::GetUnitDefID("Firebat");
    int MedicDefID = Game::Races::GetUnitDefID("Medic");
    int VultureDefID = Game::Races::GetUnitDefID("Vulture");
    int TankDefID = Game::Races::GetUnitDefID("SiegeTank");
    int GoliathDefID = Game::Races::GetUnitDefID("Goliath");
    int WraithDefID = Game::Races::GetUnitDefID("Wraith");
    int BattlecruiserDefID = Game::Races::GetUnitDefID("Battlecruiser");
    int SupplyDepotDefID = Game::Races::GetUnitDefID("SupplyDepot");
    int BarracksDefID = Game::Races::GetUnitDefID("Barracks");
    int FactoryDefID = Game::Races::GetUnitDefID("Factory");
    int StarportDefID = Game::Races::GetUnitDefID("Starport");
    int AcademyDefID = Game::Races::GetUnitDefID("Academy");
    int MachineShopDefID = Game::Races::GetUnitDefID("MachineShop");
    int ControlTowerDefID = Game::Races::GetUnitDefID("ControlTower");
    int ScienceFacilityDefID = Game::Races::GetUnitDefID("ScienceFacility");
    int PhysicsLabDefID = Game::Races::GetUnitDefID("PhysicsLab");
    int ArmoryDefID = Game::Races::GetUnitDefID("Armory");
    int EngineeringBayDefID = Game::Races::GetUnitDefID("EngineeringBay");

    auto& Res = HarvestSystem::GetResources(mAIPlayerID);
    int scvCount = CountByDefID(EM, mAIPlayerID, SvcDefID);
    int supplyDepotCount = CountByDefID(EM, mAIPlayerID, SupplyDepotDefID);
    int barracksCount = CountByDefID(EM, mAIPlayerID, BarracksDefID);
    int factoryCount = CountByDefID(EM, mAIPlayerID, FactoryDefID);
    int starportCount = CountByDefID(EM, mAIPlayerID, StarportDefID);
    int academyCount = CountByDefID(EM, mAIPlayerID, AcademyDefID);

    int marineCount = CountByDefID(EM, mAIPlayerID, MarineDefID);
    int tankCount = CountByDefID(EM, mAIPlayerID, TankDefID);
    int goliathCount = CountByDefID(EM, mAIPlayerID, GoliathDefID);
    int wraithCount = CountByDefID(EM, mAIPlayerID, WraithDefID);

    // Stage progression
    if (mStage < 1 && scvCount >= 12 && barracksCount >= 2) mStage = 1;
    if (mStage < 2 && factoryCount >= 1 && tankCount >= 4) mStage = 2;
    if (mStage < 3 && starportCount >= 1 && wraithCount >= 2) mStage = 3;

    // Always: workers + supply
    TrainWorkerIfNeeded(EM, Game::Races::GetUnitDefID("CommandCenter"), SvcDefID, 20);
    if (scvCount >= 8 && supplyDepotCount < 3 && Res.Minerals >= 100)
    {
        int idleSCV = FindIdleWorker(EM);
        if (idleSCV >= 0)
            BuildingSystem::StartBuild(EM, idleSCV, SupplyDepotDefID,
                Core::Math::Vec2(mAiBasePos.X + 60.0f * (float)(supplyDepotCount + 1),
                                 mAiBasePos.Y));
    }

    // Stage 0: Barracks + Academy
    if (mStage < 1)
    {
        if (barracksCount < 2 && Res.Minerals >= 150)
        {
            int idleSCV = FindIdleWorker(EM);
            if (idleSCV >= 0)
                BuildingSystem::StartBuild(EM, idleSCV, BarracksDefID,
                    Core::Math::Vec2(mAiBasePos.X + 100.0f, mAiBasePos.Y + 80.0f * (float)(barracksCount + 1)));
        }
        if (academyCount < 1 && barracksCount >= 1 && Res.Minerals >= 150 && Res.VespeneGas >= 0)
        {
            int idleSCV = FindIdleWorker(EM);
            if (idleSCV >= 0)
                BuildingSystem::StartBuild(EM, idleSCV, AcademyDefID,
                    Core::Math::Vec2(mAiBasePos.X + 180.0f, mAiBasePos.Y));
        }
        // Train Marines
        int producer = FindProducerFor(EM, MarineDefID);
        if (producer >= 0 && marineCount < 12 && Res.Minerals >= 50)
            ProductionSystem::StartTrain(EM, producer, MarineDefID);
    }

    // Stage 1: Factory + Machine Shop
    if (mStage >= 1)
    {
        if (factoryCount < 1 && Res.Minerals >= 200 && Res.VespeneGas >= 100)
        {
            int idleSCV = FindIdleWorker(EM);
            if (idleSCV >= 0)
                BuildingSystem::StartBuild(EM, idleSCV, FactoryDefID,
                    Core::Math::Vec2(mAiBasePos.X - 100.0f, mAiBasePos.Y));
        }
        if (factoryCount >= 1 && CountByDefID(EM, mAIPlayerID, MachineShopDefID) < 1 && Res.Minerals >= 50)
        {
            int factory = FindNearestFreeBuilding(EM, FactoryDefID);
            if (factory >= 0)
                BuildingSystem::StartBuild(EM, factory, MachineShopDefID,
                    Core::Math::Vec2(mAiBasePos.X - 120.0f, mAiBasePos.Y));
        }
        // Tanks + Goliaths
        if (tankCount < 4) { int p = FindProducerFor(EM, TankDefID); if (p >= 0 && Res.Minerals >= 150 && Res.VespeneGas >= 100) ProductionSystem::StartTrain(EM, p, TankDefID); }
        if (goliathCount < 3) { int p = FindProducerFor(EM, GoliathDefID); if (p >= 0 && Res.Minerals >= 100 && Res.VespeneGas >= 50) ProductionSystem::StartTrain(EM, p, GoliathDefID); }
    }

    // Stage 2: Starport + Wraiths
    if (mStage >= 2)
    {
        if (starportCount < 1 && Res.Minerals >= 150 && Res.VespeneGas >= 100)
        {
            int idleSCV = FindIdleWorker(EM);
            if (idleSCV >= 0)
                BuildingSystem::StartBuild(EM, idleSCV, StarportDefID,
                    Core::Math::Vec2(mAiBasePos.X - 160.0f, mAiBasePos.Y - 80.0f));
        }
        if (wraithCount < 4) { int p = FindProducerFor(EM, WraithDefID); if (p >= 0 && Res.Minerals >= 150 && Res.VespeneGas >= 100) ProductionSystem::StartTrain(EM, p, WraithDefID); }
    }

    // Stage 3: Science Facility + Battlecruisers
    if (mStage >= 3)
    {
        if (CountByDefID(EM, mAIPlayerID, ScienceFacilityDefID) < 1 && Res.Minerals >= 100 && Res.VespeneGas >= 150)
        {
            int idleSCV = FindIdleWorker(EM);
            if (idleSCV >= 0)
                BuildingSystem::StartBuild(EM, idleSCV, ScienceFacilityDefID,
                    Core::Math::Vec2(mAiBasePos.X - 200.0f, mAiBasePos.Y - 160.0f));
        }
        if (CountByDefID(EM, mAIPlayerID, BattlecruiserDefID) < 2)
        { int p = FindProducerFor(EM, BattlecruiserDefID); if (p >= 0 && Res.Minerals >= 400 && Res.VespeneGas >= 300) ProductionSystem::StartTrain(EM, p, BattlecruiserDefID); }
    }

    // Send harvesters to work
    ManageEconomy(EM);

    // Attack wave
    int totalCombat = marineCount + tankCount + goliathCount + wraithCount;
    if (totalCombat >= 8)
        SendAttackWave(EM);
}

void AIBrain::RunZergAI(Core::ECS::EntityManager& EM)
{
    int LarvaDefID = Game::Races::GetUnitDefID("Larva");
    int DroneDefID = Game::Races::GetUnitDefID("Drone");
    int OverlordDefID = Game::Races::GetUnitDefID("Overlord");
    int ZerglingDefID = Game::Races::GetUnitDefID("Zergling");
    int HydraliskDefID = Game::Races::GetUnitDefID("Hydralisk");
    int MutaliskDefID = Game::Races::GetUnitDefID("Mutalisk");
    int UltraliskDefID = Game::Races::GetUnitDefID("Ultralisk");
    int LurkerDefID = Game::Races::GetUnitDefID("Lurker");
    int GuardianDefID = Game::Races::GetUnitDefID("Guardian");
    int ScourgeDefID = Game::Races::GetUnitDefID("Scourge");
    int HatcheryDefID = Game::Races::GetUnitDefID("Hatchery");
    int LairDefID = Game::Races::GetUnitDefID("Lair");
    int HiveDefID = Game::Races::GetUnitDefID("Hive");
    int SpawningPoolDefID = Game::Races::GetUnitDefID("Spawning Pool");
    int HydraliskDenDefID = Game::Races::GetUnitDefID("Hydralisk Den");
    int SpireDefID = Game::Races::GetUnitDefID("Spire");
    int EvolutionChamberDefID = Game::Races::GetUnitDefID("Evolution Chamber");
    int QueensNestDefID = Game::Races::GetUnitDefID("Queen's Nest");
    int UltraliskCavernDefID = Game::Races::GetUnitDefID("Ultralisk Cavern");
    int DefilerMoundDefID = Game::Races::GetUnitDefID("Defiler Mound");
    int ExtractorDefID = Game::Races::GetUnitDefID("Extractor");

    auto& Res = HarvestSystem::GetResources(mAIPlayerID);
    int droneCount = CountByDefID(EM, mAIPlayerID, DroneDefID);
    int overlordCount = CountByDefID(EM, mAIPlayerID, OverlordDefID);
    int hatcheryCount = CountByDefID(EM, mAIPlayerID, HatcheryDefID);
    int lairCount = CountByDefID(EM, mAIPlayerID, LairDefID);
    int poolCount = CountByDefID(EM, mAIPlayerID, SpawningPoolDefID);
    int denCount = CountByDefID(EM, mAIPlayerID, HydraliskDenDefID);
    int spireCount = CountByDefID(EM, mAIPlayerID, SpireDefID);
    int zerglingCount = CountByDefID(EM, mAIPlayerID, ZerglingDefID);
    int hydraCount = CountByDefID(EM, mAIPlayerID, HydraliskDefID);
    int mutaCount = CountByDefID(EM, mAIPlayerID, MutaliskDefID);
    int ultraCount = CountByDefID(EM, mAIPlayerID, UltraliskDefID);

    int totalHatcheries = hatcheryCount + lairCount + CountByDefID(EM, mAIPlayerID, HiveDefID);

    // Find idle larvae
    auto FindIdleLarva = [&]() -> int {
        auto Units = EM.GetEntitiesWith<UnitComponent, PlayerComponent>();
        for (int Eid : Units)
        {
            auto* U = EM.GetComponent<UnitComponent>(Eid);
            auto* P = EM.GetComponent<PlayerComponent>(Eid);
            auto* Morph = EM.GetComponent<MorphComponent>(Eid);
            if (U && P && P->PlayerID == mAIPlayerID &&
                U->ChkUnitID == 0x23 && U->HitPoints > 0 &&
                (!Morph || !Morph->IsMorphing))
                return Eid;
        }
        return -1;
    };

    // Stage progression
    if (mStage < 1 && droneCount >= 10 && poolCount >= 1) mStage = 1;
    if (mStage < 2 && lairCount >= 1 && denCount >= 1 && mutaCount >= 4) mStage = 2;
    if (mStage < 3 && CountByDefID(EM, mAIPlayerID, HiveDefID) >= 1 && ultraCount >= 2) mStage = 3;

    // Supply: morph overlords
    if (Res.SupplyUsed + 2 >= Res.SupplyMax && Res.Minerals >= 100)
    {
        int larva = FindIdleLarva();
        if (larva >= 0)
            ZergSystem::StartMorphLarvaIntoUnit(EM, larva, OverlordDefID);
    }

    // Stage 0: Drones + Spawning Pool + Zerglings
    if (mStage < 1)
    {
        // Drones up to 12
        if (droneCount < 12 && Res.Minerals >= 50)
        {
            int larva = FindIdleLarva();
            if (larva >= 0)
                ZergSystem::StartMorphLarvaIntoUnit(EM, larva, DroneDefID);
        }

        // Spawning Pool
        if (poolCount < 1 && droneCount >= 6 && Res.Minerals >= 200)
        {
            int drone = FindIdleWorker(EM);
            if (drone >= 0)
                ZergSystem::StartMorphDroneIntoBuilding(EM, drone, SpawningPoolDefID,
                    Core::Math::Vec2(mAiBasePos.X + 100.0f, mAiBasePos.Y));
        }

        // Zerglings
        if (poolCount >= 1 && zerglingCount < 12 && Res.Minerals >= 50)
        {
            int larva = FindIdleLarva();
            if (larva >= 0)
                ZergSystem::StartMorphLarvaIntoUnit(EM, larva, ZerglingDefID);
        }
    }

    // Stage 1: Lair + Hydralisk Den + Hydralisks + Mutalisks
    if (mStage >= 1)
    {
        // Morph Hatchery→Lair
        if (lairCount < 1 && hatcheryCount >= 1 && Res.Minerals >= 150 && Res.VespeneGas >= 100)
        {
            auto Buildings = EM.GetEntitiesWith<BuildingComponent, PlayerComponent>();
            for (int Bid : Buildings)
            {
                auto* B = EM.GetComponent<BuildingComponent>(Bid);
                auto* P = EM.GetComponent<PlayerComponent>(Bid);
                if (B && P && P->PlayerID == mAIPlayerID &&
                    B->BuildingDefID == HatcheryDefID && !B->UnderConstruction)
                {
                    ZergSystem::StartMorphBuilding(EM, Bid, LairDefID);
                    break;
                }
            }
        }

        // Hydralisk Den
        if (denCount < 1 && lairCount >= 1 && Res.Minerals >= 75 && Res.VespeneGas >= 50)
        {
            int drone = FindIdleWorker(EM);
            if (drone >= 0)
                ZergSystem::StartMorphDroneIntoBuilding(EM, drone, HydraliskDenDefID,
                    Core::Math::Vec2(mAiBasePos.X - 100.0f, mAiBasePos.Y));
        }

        // Hydralisks
        if (denCount >= 1 && hydraCount < 8 && Res.Minerals >= 75 && Res.VespeneGas >= 25)
        {
            int larva = FindIdleLarva();
            if (larva >= 0)
                ZergSystem::StartMorphLarvaIntoUnit(EM, larva, HydraliskDefID);
        }

        // Spire
        if (spireCount < 1 && lairCount >= 1 && Res.Minerals >= 100 && Res.VespeneGas >= 100)
        {
            int drone = FindIdleWorker(EM);
            if (drone >= 0)
                ZergSystem::StartMorphDroneIntoBuilding(EM, drone, SpireDefID,
                    Core::Math::Vec2(mAiBasePos.X - 140.0f, mAiBasePos.Y));
        }

        // Mutalisks
        if (spireCount >= 1 && mutaCount < 6 && Res.Minerals >= 100 && Res.VespeneGas >= 100)
        {
            int larva = FindIdleLarva();
            if (larva >= 0)
                ZergSystem::StartMorphLarvaIntoUnit(EM, larva, MutaliskDefID);
        }
    }

    // Stage 2: Hive + Ultralisk Cavern + Ultralisks
    if (mStage >= 2)
    {
        if (CountByDefID(EM, mAIPlayerID, HiveDefID) < 1 && lairCount >= 1 &&
            Res.Minerals >= 200 && Res.VespeneGas >= 150)
        {
            auto Buildings = EM.GetEntitiesWith<BuildingComponent, PlayerComponent>();
            for (int Bid : Buildings)
            {
                auto* B = EM.GetComponent<BuildingComponent>(Bid);
                auto* P = EM.GetComponent<PlayerComponent>(Bid);
                if (B && P && P->PlayerID == mAIPlayerID &&
                    B->BuildingDefID == LairDefID && !B->UnderConstruction)
                {
                    ZergSystem::StartMorphBuilding(EM, Bid, HiveDefID);
                    break;
                }
            }
        }

        if (CountByDefID(EM, mAIPlayerID, UltraliskCavernDefID) < 1 &&
            CountByDefID(EM, mAIPlayerID, HiveDefID) >= 1 &&
            Res.Minerals >= 150 && Res.VespeneGas >= 200)
        {
            int drone = FindIdleWorker(EM);
            if (drone >= 0)
                ZergSystem::StartMorphDroneIntoBuilding(EM, drone, UltraliskCavernDefID,
                    Core::Math::Vec2(mAiBasePos.X - 180.0f, mAiBasePos.Y - 80.0f));
        }

        if (ultraCount < 4 && CountByDefID(EM, mAIPlayerID, UltraliskCavernDefID) >= 1 &&
            Res.Minerals >= 200 && Res.VespeneGas >= 200)
        {
            int larva = FindIdleLarva();
            if (larva >= 0)
                ZergSystem::StartMorphLarvaIntoUnit(EM, larva, UltraliskDefID);
        }
    }

    // Extra hatcheries for larva production
    if (droneCount >= 14 && totalHatcheries < 2 && Res.Minerals >= 300)
    {
        int drone = FindIdleWorker(EM);
        if (drone >= 0)
            ZergSystem::StartMorphDroneIntoBuilding(EM, drone, HatcheryDefID,
                Core::Math::Vec2(mAiBasePos.X + 300.0f, mAiBasePos.Y));
    }

    ManageEconomy(EM);

    int totalCombat = zerglingCount + hydraCount + mutaCount + ultraCount;
    if (totalCombat >= 10)
        SendAttackWave(EM);
}

void AIBrain::RunProtossAI(Core::ECS::EntityManager& EM)
{
    int ProbeDefID = Game::Races::GetUnitDefID("Probe");
    int ZealotDefID = Game::Races::GetUnitDefID("Zealot");
    int DragoonDefID = Game::Races::GetUnitDefID("Dragoon");
    int HighTemplarDefID = Game::Races::GetUnitDefID("High Templar");
    int ReaverDefID = Game::Races::GetUnitDefID("Reaver");
    int ScoutDefID = Game::Races::GetUnitDefID("Scout");
    int CorsairDefID = Game::Races::GetUnitDefID("Corsair");
    int CarrierDefID = Game::Races::GetUnitDefID("Carrier");
    int NexusDefID = Game::Races::GetUnitDefID("Nexus");
    int PylonDefID = Game::Races::GetUnitDefID("Pylon");
    int GatewayDefID = Game::Races::GetUnitDefID("Gateway");
    int ForgeDefID = Game::Races::GetUnitDefID("Forge");
    int CyberneticsCoreDefID = Game::Races::GetUnitDefID("Cybernetics Core");
    int CitadelOfAdunDefID = Game::Races::GetUnitDefID("Citadel of Adun");
    int TemplarArchivesDefID = Game::Races::GetUnitDefID("Templar Archives");
    int RoboticsFacilityDefID = Game::Races::GetUnitDefID("Robotics Facility");
    int StargateDefID = Game::Races::GetUnitDefID("Stargate");
    int FleetBeaconDefID = Game::Races::GetUnitDefID("Fleet Beacon");

    auto& Res = HarvestSystem::GetResources(mAIPlayerID);
    int probeCount = CountByDefID(EM, mAIPlayerID, ProbeDefID);
    int pylonCount = CountByDefID(EM, mAIPlayerID, PylonDefID);
    int gatewayCount = CountByDefID(EM, mAIPlayerID, GatewayDefID);
    int coreCount = CountByDefID(EM, mAIPlayerID, CyberneticsCoreDefID);
    int citadelCount = CountByDefID(EM, mAIPlayerID, CitadelOfAdunDefID);
    int stargateCount = CountByDefID(EM, mAIPlayerID, StargateDefID);
    int roboCount = CountByDefID(EM, mAIPlayerID, RoboticsFacilityDefID);
    int zealotCount = CountByDefID(EM, mAIPlayerID, ZealotDefID);
    int dragoonCount = CountByDefID(EM, mAIPlayerID, DragoonDefID);
    int scoutCount = CountByDefID(EM, mAIPlayerID, ScoutDefID);
    int carrierCount = CountByDefID(EM, mAIPlayerID, CarrierDefID);

    // Stage progression
    if (mStage < 1 && gatewayCount >= 1 && zealotCount >= 4) mStage = 1;
    if (mStage < 2 && coreCount >= 1 && dragoonCount >= 4) mStage = 2;
    if (mStage < 3 && stargateCount >= 1 && carrierCount >= 1) mStage = 3;

    // Probes from Nexus
    TrainWorkerIfNeeded(EM, NexusDefID, ProbeDefID, 18);

    // Pylons for power and supply
    if (pylonCount < 4 && probeCount >= 6 && Res.Minerals >= 100)
    {
        int probe = FindIdleWorker(EM);
        if (probe >= 0)
            BuildingSystem::StartBuild(EM, probe, PylonDefID,
                Core::Math::Vec2(mAiBasePos.X + 60.0f * (float)(pylonCount),
                                 mAiBasePos.Y + 80.0f));
    }

    // Stage 0: Gateway + Zealots
    if (mStage < 1)
    {
        if (gatewayCount < 2 && pylonCount >= 1 && Res.Minerals >= 150)
        {
            int probe = FindIdleWorker(EM);
            if (probe >= 0)
                BuildingSystem::StartBuild(EM, probe, GatewayDefID,
                    Core::Math::Vec2(mAiBasePos.X + 100.0f,
                                     mAiBasePos.Y + 80.0f * (float)(gatewayCount + 1)));
        }
        if (zealotCount < 8)
        { int p = FindProducerFor(EM, ZealotDefID); if (p >= 0 && Res.Minerals >= 100) ProductionSystem::StartTrain(EM, p, ZealotDefID); }
    }

    // Stage 1: Cybernetics Core + Dragoons + Citadel
    if (mStage >= 1)
    {
        if (coreCount < 1 && gatewayCount >= 1 && Res.Minerals >= 200)
        {
            int probe = FindIdleWorker(EM);
            if (probe >= 0)
                BuildingSystem::StartBuild(EM, probe, CyberneticsCoreDefID,
                    Core::Math::Vec2(mAiBasePos.X - 100.0f, mAiBasePos.Y));
        }
        if (dragoonCount < 6 && coreCount >= 1)
        { int p = FindProducerFor(EM, DragoonDefID); if (p >= 0 && Res.Minerals >= 125 && Res.VespeneGas >= 50) ProductionSystem::StartTrain(EM, p, DragoonDefID); }

        if (citadelCount < 1 && coreCount >= 1 && Res.Minerals >= 150 && Res.VespeneGas >= 100)
        {
            int probe = FindIdleWorker(EM);
            if (probe >= 0)
                BuildingSystem::StartBuild(EM, probe, CitadelOfAdunDefID,
                    Core::Math::Vec2(mAiBasePos.X - 140.0f, mAiBasePos.Y));
        }
    }

    // Stage 2: Stargate + Scouts + Robotics
    if (mStage >= 2)
    {
        if (stargateCount < 1 && coreCount >= 1 && Res.Minerals >= 150 && Res.VespeneGas >= 150)
        {
            int probe = FindIdleWorker(EM);
            if (probe >= 0)
                BuildingSystem::StartBuild(EM, probe, StargateDefID,
                    Core::Math::Vec2(mAiBasePos.X - 180.0f, mAiBasePos.Y + 80.0f));
        }
        if (scoutCount < 4 && stargateCount >= 1)
        { int p = FindProducerFor(EM, ScoutDefID); if (p >= 0 && Res.Minerals >= 275 && Res.VespeneGas >= 125) ProductionSystem::StartTrain(EM, p, ScoutDefID); }

        if (CountByDefID(EM, mAIPlayerID, FleetBeaconDefID) < 1 && stargateCount >= 1 &&
            Res.Minerals >= 200 && Res.VespeneGas >= 150)
        {
            int probe = FindIdleWorker(EM);
            if (probe >= 0)
                BuildingSystem::StartBuild(EM, probe, FleetBeaconDefID,
                    Core::Math::Vec2(mAiBasePos.X - 220.0f, mAiBasePos.Y));
        }
    }

    // Stage 3: Carriers + Robotics
    if (mStage >= 3)
    {
        if (carrierCount < 2 && CountByDefID(EM, mAIPlayerID, FleetBeaconDefID) >= 1)
        { int p = FindProducerFor(EM, CarrierDefID); if (p >= 0 && Res.Minerals >= 350 && Res.VespeneGas >= 250) ProductionSystem::StartTrain(EM, p, CarrierDefID); }

        if (roboCount < 1 && coreCount >= 1 && Res.Minerals >= 200 && Res.VespeneGas >= 100)
        {
            int probe = FindIdleWorker(EM);
            if (probe >= 0)
                BuildingSystem::StartBuild(EM, probe, RoboticsFacilityDefID,
                    Core::Math::Vec2(mAiBasePos.X - 260.0f, mAiBasePos.Y));
        }
    }

    ManageEconomy(EM);

    int totalCombat = zealotCount + dragoonCount + scoutCount + carrierCount;
    if (totalCombat >= 8)
        SendAttackWave(EM);
}

void AIBrain::ManageEconomy(Core::ECS::EntityManager& EM)
{
    int NearestMineral = -1;
    float BestDist = 1e9f;
    auto Minerals = EM.GetEntitiesWith<TransformComponent, ResourceComponent>();
    for (int Mid : Minerals)
    {
        auto* RT = EM.GetComponent<TransformComponent>(Mid);
        auto* Res = EM.GetComponent<ResourceComponent>(Mid);
        if (!RT || !Res || Res->Type != ResourceType::Minerals || Res->IsDepleted) continue;
        float D = RT->Position.DistanceTo(mAiBasePos);
        if (D < BestDist) { BestDist = D; NearestMineral = Mid; }
    }
    if (NearestMineral < 0) return;

    auto Units = EM.GetEntitiesWith<UnitComponent, PlayerComponent, HarvestComponent>();
    for (int Eid : Units)
    {
        auto* U = EM.GetComponent<UnitComponent>(Eid);
        auto* P = EM.GetComponent<PlayerComponent>(Eid);
        auto* H = EM.GetComponent<HarvestComponent>(Eid);
        if (!U || !P || !H) continue;
        if (P->PlayerID != mAIPlayerID) continue;
        if (H->State == HarvestState::Idle)
            HarvestSystem::IssueHarvest(EM, {Eid}, NearestMineral);
    }
}

void AIBrain::ManageMilitary(Core::ECS::EntityManager& EM)
{
    (void)EM;
}

void AIBrain::SendAttackWave(Core::ECS::EntityManager& EM)
{
    std::vector<int> Attackers;
    auto Units = EM.GetEntitiesWith<UnitComponent, PlayerComponent, CombatComponent, TransformComponent>();
    for (int Eid : Units)
    {
        auto* U = EM.GetComponent<UnitComponent>(Eid);
        auto* P = EM.GetComponent<PlayerComponent>(Eid);
        auto* C = EM.GetComponent<CombatComponent>(Eid);
        if (!U || !P || !C) continue;
        if (P->PlayerID != mAIPlayerID) continue;
        if (U->HitPoints <= 0) continue;
        if (C->GroundDamage > 0 || C->AirDamage > 0)
            Attackers.push_back(Eid);
    }

    if ((int)Attackers.size() >= 8)
    {
        CombatSystem::IssueAttackMove(EM, Attackers, mEnemyBasePos);
        mAttackWaveCount++;
    }
}

void AIBrain::BuildOrTrain(Core::ECS::EntityManager& EM, int DefID, const Core::Math::Vec2& NearPos)
{
    const auto& Def = Game::Races::GetUnitDefByID(DefID);
    if (Def.IsBuilding)
    {
        int Builder = FindIdleWorker(EM);
        if (Builder >= 0)
            BuildingSystem::StartBuild(EM, Builder, DefID, NearPos);
    }
    else
    {
        int Producer = FindProducerFor(EM, DefID);
        if (Producer >= 0)
            ProductionSystem::StartTrain(EM, Producer, DefID);
    }
}

void AIBrain::Update(float DeltaTime)
{
    if (DeltaTime <= 0.0f) return;
    auto& EM = mSystemMgr.GetEntityManager();

    if (!mPositionsResolved)
    {
        mAiBasePos = FindBaseLocation(EM);
        mEnemyBasePos = FindEnemyBaseLocation(EM);
        mExpansionPos = FindExpansionLocation(EM);
        if (mAiBasePos.LengthSq() > 0)
            mPositionsResolved = true;
    }

    mDecisionTimer += DeltaTime;
    if (mDecisionTimer < mDecisionInterval) return;
    mDecisionTimer = 0.0f;

    int RaceVal = GetRace(EM);
    switch (RaceVal)
    {
        case 0: RunTerranAI(EM); break;
        case 1: RunZergAI(EM); break;
        case 2: RunProtossAI(EM); break;
        default: RunTerranAI(EM); break;
    }
}

} // namespace Game::Systems
