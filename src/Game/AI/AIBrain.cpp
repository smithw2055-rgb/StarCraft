#include "Game/AI/AIBrain.h"
#include "Game/Systems/HarvestSystem.h"
#include "Game/Systems/ProductionSystem.h"
#include "Game/Systems/BuildingSystem.h"
#include "Game/Systems/CommandSystem.h"
#include "Game/Systems/CombatSystem.h"
#include "Game/Systems/ZergSystem.h"
#include "Game/Systems/ProtossSystem.h"
#include "Game/Components.h"
#include "Game/Races/RaceDef.h"
#include "Game/Map/Map.h"
#include "Core/Utils/Logger.h"

#include <algorithm>
#include <cstdlib>
#include <limits>

namespace Game::Systems
{

using namespace Game::Components;

namespace
{
    constexpr float kAttackWaveCooldownSeconds = 20.0f;
    constexpr float kAttackGroupMaxAgeSeconds = 90.0f;

    int NormalizeDefIDForRace(int DefID, Race RaceType)
    {
        if (DefID < 0)
            return DefID;

        const auto Candidate = Game::Races::GetUnitDefByID(DefID);
        if (Candidate.Name != "Unknown" && Candidate.RaceType == RaceType)
            return DefID;

        if (RaceType == Race::Zerg)
            return (int)Game::Races::GetTerranDefs().size() + DefID;
        if (RaceType == Race::Protoss)
            return (int)Game::Races::GetTerranDefs().size() +
                   (int)Game::Races::GetZergDefs().size() + DefID;
        return DefID;
    }

    int ResolveOwnedDefID(Core::ECS::EntityManager& EM, int EntityID)
    {
        if (auto* Building = EM.GetComponent<BuildingComponent>(EntityID);
            Building && Building->BuildingDefID >= 0)
        {
            return Building->BuildingDefID;
        }

        if (auto* Unit = EM.GetComponent<UnitComponent>(EntityID))
            return Game::Races::GetUnitDefIDByChkUnitID(Unit->ChkUnitID);
        return -1;
    }

    bool IsResourceDepotDefID(int DefID)
    {
        const auto Def = Game::Races::GetUnitDefByID(DefID);
        return Def.Name == "CommandCenter" ||
               Def.Name == "Hatchery" ||
               Def.Name == "Lair" ||
               Def.Name == "Hive" ||
               Def.Name == "Nexus";
    }

    bool IsWorkerIdle(Core::ECS::EntityManager& EM, int EntityID)
    {
        if (auto* Command = EM.GetComponent<CommandComponent>(EntityID))
        {
            const auto* Order = Command->GetCurrentOrder();
            if (Order && Order->Type == OrderType::Repair)
                return false;
        }
        if (auto* Harvest = EM.GetComponent<HarvestComponent>(EntityID))
            return Harvest->State == HarvestState::Idle;

        auto* Command = EM.GetComponent<CommandComponent>(EntityID);
        return !Command || Command->CurrentCommand == CommandType::Stop;
    }

    bool IsAssignedToConstruction(Core::ECS::EntityManager& EM, int EntityID)
    {
        if (BuildingSystem::IsBuilderCommitted(EM, EntityID))
            return true;
        for (int BuildingID : EM.GetEntitiesWith<BuildingComponent>())
        {
            auto* Building = EM.GetComponent<BuildingComponent>(BuildingID);
            if (Building && Building->UnderConstruction &&
                Building->HasBuilderUnit(EntityID))
            {
                return true;
            }
        }
        return false;
    }

    bool IsWorkerRepairing(Core::ECS::EntityManager& EM, int EntityID)
    {
        auto* Command = EM.GetComponent<CommandComponent>(EntityID);
        const auto* Order = Command ? Command->GetCurrentOrder() : nullptr;
        return Order && Order->Type == OrderType::Repair;
    }

    bool IsCombatUnit(const UnitComponent& Unit, const CombatComponent& Combat)
    {
        return Unit.HitPoints > 0 && (Combat.GroundDamage > 0 || Combat.AirDamage > 0);
    }

    bool IsWorkerUnit(const UnitComponent& Unit)
    {
        return Unit.ChkUnitID == 0x07 || Unit.ChkUnitID == 0x29 ||
               Unit.ChkUnitID == 0x40;
    }

    bool IsTerranMechanicalUnit(const UnitComponent& Unit)
    {
        if (Unit.UnitRace != Race::Terran || IsWorkerUnit(Unit))
            return false;
        const int DefID = Game::Races::GetUnitDefIDByChkUnitID(Unit.ChkUnitID);
        if (DefID < 0)
            return false;
        const auto Def = Game::Races::GetUnitDefByID(DefID);
        return !Def.IsBuilding && Def.Name != "Marine" &&
               Def.Name != "Firebat" && Def.Name != "Medic" &&
               Def.Name != "Ghost";
    }

    bool IsTerranProductionBuilding(const std::string& Name)
    {
        return Name == "Barracks" || Name == "Factory" ||
               Name == "Starport" || Name == "MachineShop" ||
               Name == "ControlTower" || Name == "Armory";
    }
}

AIBrain::AIBrain(Core::ECS::SystemManager& SysMgr, int AIPlayerID, int TargetPlayerID,
                 bool ShowcaseMode, const Game::Map::GameMap* Map)
    : mSystemMgr(SysMgr)
    , mAIPlayerID(AIPlayerID)
    , mTargetPlayerID(TargetPlayerID)
    , mShowcaseMode(ShowcaseMode)
    , mMap(Map)
{
}

Core::Math::Vec2 AIBrain::FindBaseLocation(Core::ECS::EntityManager& EM)
{
    auto Entities = EM.GetEntitiesWith<TransformComponent, PlayerComponent, BuildingComponent>();
    Core::Math::Vec2 Fallback(1000.0f, 1000.0f);
    for (int Eid : Entities)
    {
        auto* Player = EM.GetComponent<PlayerComponent>(Eid);
        auto* Trans = EM.GetComponent<TransformComponent>(Eid);
        auto* Building = EM.GetComponent<BuildingComponent>(Eid);
        if (!Player || Player->PlayerID != mAIPlayerID || !Trans || !Building ||
            Building->UnderConstruction || Building->IsAddon)
        {
            continue;
        }

        if (IsResourceDepotDefID(ResolveOwnedDefID(EM, Eid)))
            return Trans->Position;

        Fallback = Trans->Position;
    }
    return Fallback;
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

    // Once the base is gone, pursue a surviving unit instead of repeatedly
    // attacking the destroyed base coordinates. This lets simple AI close out
    // a melee match without adding scouting or multi-pronged tactics.
    for (int Eid : EM.GetEntitiesWith<TransformComponent, PlayerComponent,
                                      UnitComponent>())
    {
        auto* Player = EM.GetComponent<PlayerComponent>(Eid);
        auto* Trans = EM.GetComponent<TransformComponent>(Eid);
        auto* Unit = EM.GetComponent<UnitComponent>(Eid);
        if (Player && Player->PlayerID == mTargetPlayerID && Trans && Unit &&
            Unit->HitPoints > 0 && !EM.GetComponent<ResourceComponent>(Eid))
        {
            return Trans->Position;
        }
    }
    return Core::Math::Vec2(2000.0f, 2000.0f);
}

bool AIBrain::HasLivingForces(Core::ECS::EntityManager& EM, int PlayerID) const
{
    for (int Eid : EM.GetEntitiesWith<UnitComponent, PlayerComponent>())
    {
        auto* Unit = EM.GetComponent<UnitComponent>(Eid);
        auto* Player = EM.GetComponent<PlayerComponent>(Eid);
        if (Unit && Player && Player->PlayerID == PlayerID && Unit->HitPoints > 0 &&
            !EM.GetComponent<ResourceComponent>(Eid))
        {
            return true;
        }
    }
    return false;
}

void AIBrain::RetargetIfNeeded(Core::ECS::EntityManager& EM)
{
    if (HasLivingForces(EM, mTargetPlayerID))
        return;

    const int PreviousTarget = mTargetPlayerID;
    for (int PlayerID = 0; PlayerID < 8; ++PlayerID)
    {
        if (PlayerID == mAIPlayerID || !HasLivingForces(EM, PlayerID))
            continue;
        mTargetPlayerID = PlayerID;
        mEnemyBasePos = FindEnemyBaseLocation(EM);
        mActiveAttackGroup.clear();
        mActiveAttackGroupAge = 0.0f;
        mAttackCooldownRemaining = 0.0f;
        Core::Utils::Logger::Info(
            "AIBrain", "Player " + std::to_string(mAIPlayerID) +
            " retargeted from player " + std::to_string(PreviousTarget) +
            " to player " + std::to_string(mTargetPlayerID));
        return;
    }
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

int AIBrain::CountByDefID(Core::ECS::EntityManager& EM, int PlayerID, int DefID,
                          bool IncludeUnderConstruction)
{
    int Count = IncludeUnderConstruction
        ? BuildingSystem::CountPendingBuilds(PlayerID, DefID) : 0;
    auto Entities = EM.GetEntitiesWith<PlayerComponent>();
    for (int Eid : Entities)
    {
        auto* P = EM.GetComponent<PlayerComponent>(Eid);
        if (!P || P->PlayerID != PlayerID) continue;

        auto* Bldg = EM.GetComponent<BuildingComponent>(Eid);
        auto* Unit = EM.GetComponent<UnitComponent>(Eid);

        if (Bldg)
        {
            if (Bldg->BuildingDefID == DefID &&
                (IncludeUnderConstruction || !Bldg->UnderConstruction))
            {
                Count++;
            }
            continue;
        }

        if (Unit && Unit->HitPoints > 0 && Unit->ChkUnitID ==
            Game::Races::GetUnitDefByID(DefID).ChkUnitID)
        {
            Count++;
        }
    }
    return Count;
}

int AIBrain::CountQueuedByDefID(Core::ECS::EntityManager& EM,
                                int PlayerID, int DefID) const
{
    int Count = 0;
    for (int Eid : EM.GetEntitiesWith<ProductionComponent, PlayerComponent>())
    {
        auto* Production = EM.GetComponent<ProductionComponent>(Eid);
        auto* Player = EM.GetComponent<PlayerComponent>(Eid);
        if (!Production || !Player || Player->PlayerID != PlayerID)
            continue;
        Count += (int)std::count(
            Production->Queue.begin(), Production->Queue.end(), DefID);
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
    const int ProducerDefID = NormalizeDefIDForRace(Def.ProducedBy, Def.RaceType);

    int BestProducer = -1;
    size_t BestQueueSize = std::numeric_limits<size_t>::max();
    auto Buildings = EM.GetEntitiesWith<BuildingComponent, PlayerComponent>();
    for (int Bid : Buildings)
    {
        auto* B = EM.GetComponent<BuildingComponent>(Bid);
        auto* P = EM.GetComponent<PlayerComponent>(Bid);
        if (B && P && P->PlayerID == mAIPlayerID &&
            B->BuildingDefID == ProducerDefID && !B->UnderConstruction &&
            ProductionSystem::CanTrain(EM, Bid, UnitDefID))
        {
            auto* Production = EM.GetComponent<ProductionComponent>(Bid);
            const size_t QueueSize = Production ? Production->Queue.size() : 0u;
            if (QueueSize < BestQueueSize)
            {
                BestProducer = Bid;
                BestQueueSize = QueueSize;
            }
        }
    }
    // Keep every production building active without reserving an excessive
    // amount of resources in one long queue.
    return BestQueueSize < 2u ? BestProducer : -1;
}

int AIBrain::FindIdleWorker(Core::ECS::EntityManager& EM)
{
    int RaceVal = GetRace(EM);
    int WorkerChkIDs[] = {0x07, 0x29, 0x40}; // SCV, Drone, Probe

    auto Units = EM.GetEntitiesWith<UnitComponent, PlayerComponent>();
    for (int Eid : Units)
    {
        auto* U = EM.GetComponent<UnitComponent>(Eid);
        auto* P = EM.GetComponent<PlayerComponent>(Eid);
        if (!U || !P) continue;
        if (P->PlayerID != mAIPlayerID) continue;
        if (U->HitPoints <= 0) continue;
        if (U->ChkUnitID != WorkerChkIDs[RaceVal]) continue;
        if (auto* Morph = EM.GetComponent<MorphComponent>(Eid); Morph && Morph->IsMorphing)
            continue;
        if (IsAssignedToConstruction(EM, Eid))
            continue;
        if (IsWorkerIdle(EM, Eid))
            return Eid;
    }
    return -1;
}

bool AIBrain::FindBuildPositionNear(Core::ECS::EntityManager& EM, int BuildingDefID,
                                    const Core::Math::Vec2& NearPos, Core::Math::Vec2& OutPos)
{
    const int BaseHeight = mMap ? mMap->GetGroundHeightAtWorld(mAiBasePos) : -1;
    const auto IsOnBasePlateau = [&](const Core::Math::Vec2& Candidate)
    {
        if (!mShowcaseMode || !mMap)
            return true;
        // Keep the showcase compact and entirely on the mineral base's high
        // ground. Sampling around the center also rejects footprints that
        // straddle a cliff even when their center is still on the plateau.
        if (Candidate.DistanceTo(mAiBasePos) > 760.0f)
            return false;
        constexpr float HalfExtent = 64.0f;
        for (float Y : {-HalfExtent, 0.0f, HalfExtent})
        {
            for (float X : {-HalfExtent, 0.0f, HalfExtent})
            {
                const Core::Math::Vec2 Sample{Candidate.X + X, Candidate.Y + Y};
                int TileX = 0;
                int TileY = 0;
                mMap->WorldToTile(Sample, TileX, TileY);
                if (!mMap->IsInBounds(TileX, TileY) ||
                    mMap->GetGroundHeight(TileX, TileY) != BaseHeight)
                {
                    return false;
                }
            }
        }
        return true;
    };

    const float Step = 64.0f;
    if (IsOnBasePlateau(NearPos) &&
        BuildingSystem::IsPlacementValid(EM, BuildingDefID, NearPos, mAIPlayerID))
    {
        OutPos = NearPos;
        return true;
    }

    for (int Radius = 1; Radius <= 5; ++Radius)
    {
        for (int Y = -Radius; Y <= Radius; ++Y)
        {
            for (int X = -Radius; X <= Radius; ++X)
            {
                if (std::max(std::abs(X), std::abs(Y)) != Radius)
                    continue;

                const Core::Math::Vec2 Candidate(
                    NearPos.X + (float)X * Step,
                    NearPos.Y + (float)Y * Step);
                if (IsOnBasePlateau(Candidate) && BuildingSystem::IsPlacementValid(
                        EM, BuildingDefID, Candidate, mAIPlayerID))
                {
                    OutPos = Candidate;
                    return true;
                }
            }
        }
    }

    return false;
}

bool AIBrain::StartBuildingNear(Core::ECS::EntityManager& EM, int BuilderID,
                                int BuildingDefID, const Core::Math::Vec2& NearPos)
{
    const auto& Def = Game::Races::GetUnitDefByID(BuildingDefID);
    if (Def.IsAddon)
        return BuildingSystem::StartBuild(EM, BuilderID, BuildingDefID, BuildTarget{});

    Core::Math::Vec2 BuildPos;
    if (!FindBuildPositionNear(EM, BuildingDefID, NearPos, BuildPos))
        return false;
    return BuildingSystem::StartBuild(EM, BuilderID, BuildingDefID, BuildPos);
}

bool AIBrain::StartZergBuildingNear(Core::ECS::EntityManager& EM, int DroneID,
                                    int BuildingDefID, const Core::Math::Vec2& NearPos)
{
    Core::Math::Vec2 BuildPos;
    if (!FindBuildPositionNear(EM, BuildingDefID, NearPos, BuildPos))
        return false;
    if (!ZergSystem::CanMorphDroneIntoBuilding(EM, DroneID, BuildingDefID))
        return false;
    CommandSystem::InterruptHarvest(EM, DroneID);
    return ZergSystem::StartMorphDroneIntoBuilding(EM, DroneID, BuildingDefID, BuildPos);
}

bool AIBrain::TrainUnitIfPossible(Core::ECS::EntityManager& EM, int UnitDefID)
{
    int Producer = FindProducerFor(EM, UnitDefID);
    return Producer >= 0 && ProductionSystem::StartTrain(EM, Producer, UnitDefID);
}

int AIBrain::FindAvailableAddonParent(Core::ECS::EntityManager& EM, int ParentDefID)
{
    auto Buildings = EM.GetEntitiesWith<BuildingComponent, PlayerComponent>();
    for (int Bid : Buildings)
    {
        auto* B = EM.GetComponent<BuildingComponent>(Bid);
        auto* P = EM.GetComponent<PlayerComponent>(Bid);
        if (B && P && P->PlayerID == mAIPlayerID &&
            B->BuildingDefID == ParentDefID && !B->UnderConstruction &&
            (B->AddonID < 0 || !EM.IsAlive(B->AddonID)))
        {
            return Bid;
        }
    }
    return -1;
}

int AIBrain::FindTerranBuilderFor(Core::ECS::EntityManager& EM, int BuildingDefID)
{
    const int ScvDefID = Game::Races::GetUnitDefID("SCV");
    int MineralWorker = -1;
    for (int Eid : EM.GetEntitiesWith<UnitComponent, PlayerComponent>())
    {
        auto* Unit = EM.GetComponent<UnitComponent>(Eid);
        auto* Player = EM.GetComponent<PlayerComponent>(Eid);
        if (!Unit || !Player || Player->PlayerID != mAIPlayerID ||
            Unit->HitPoints <= 0 ||
            Game::Races::GetUnitDefIDByChkUnitID(Unit->ChkUnitID) != ScvDefID)
        {
            continue;
        }
        if (IsWorkerRepairing(EM, Eid))
            continue;
        // Never pull one of the deliberately saturated gas workers for normal
        // construction; use an idle SCV first, then a mineral worker.
        if (const auto* Harvest = EM.GetComponent<HarvestComponent>(Eid))
        {
            const int ResourceID = Harvest->DesiredResourceID >= 0
                ? Harvest->DesiredResourceID
                : Harvest->TargetResourceID;
            const auto* Resource = EM.GetComponent<ResourceComponent>(ResourceID);
            if (Resource && Resource->Type == ResourceType::VespeneGas)
                continue;
        }
        // Placement is resolved after selection.  This check also rejects an
        // SCV already assigned to an unfinished Terran structure while still
        // allowing the brain to pull a normal mineral harvester.
        if (BuildingSystem::CanBuild(
                EM, Eid, BuildingDefID, BuildTarget{}, nullptr, false))
        {
            if (IsWorkerIdle(EM, Eid))
                return Eid;
            if (MineralWorker < 0)
                MineralWorker = Eid;
        }
    }
    return MineralWorker;
}

int AIBrain::FindZergBuilderFor(Core::ECS::EntityManager& EM, int BuildingDefID)
{
    const int DroneDefID = Game::Races::GetUnitDefID("Drone");
    const int ExtractorDefID = Game::Races::GetUnitDefID("Extractor");
    for (int Eid : EM.GetEntitiesWith<UnitComponent, PlayerComponent>())
    {
        auto* Unit = EM.GetComponent<UnitComponent>(Eid);
        auto* Player = EM.GetComponent<PlayerComponent>(Eid);
        if (!Unit || !Player || Player->PlayerID != mAIPlayerID ||
            Unit->HitPoints <= 0 ||
            Game::Races::GetUnitDefIDByChkUnitID(Unit->ChkUnitID) != DroneDefID)
        {
            continue;
        }
        if (const auto* Harvest = EM.GetComponent<HarvestComponent>(Eid))
        {
            const int ResourceID = Harvest->DesiredResourceID >= 0
                ? Harvest->DesiredResourceID
                : Harvest->TargetResourceID;
            const auto* Resource = EM.GetComponent<ResourceComponent>(ResourceID);
            if (Resource && Resource->Type == ResourceType::VespeneGas)
                continue;
        }
        const bool CanBuild = BuildingDefID == ExtractorDefID
            ? BuildingSystem::CanBuild(
                EM, Eid, BuildingDefID, BuildTarget{}, nullptr, false)
            : ZergSystem::CanMorphDroneIntoBuilding(EM, Eid, BuildingDefID);
        if (CanBuild)
            return Eid;
    }
    return -1;
}

int AIBrain::FindProtossBuilderFor(Core::ECS::EntityManager& EM, int BuildingDefID)
{
    const int ProbeDefID = Game::Races::GetUnitDefID("Probe");
    for (int Eid : EM.GetEntitiesWith<UnitComponent, PlayerComponent>())
    {
        auto* Unit = EM.GetComponent<UnitComponent>(Eid);
        auto* Player = EM.GetComponent<PlayerComponent>(Eid);
        if (!Unit || !Player || Player->PlayerID != mAIPlayerID ||
            Unit->HitPoints <= 0 ||
            Game::Races::GetUnitDefIDByChkUnitID(Unit->ChkUnitID) != ProbeDefID)
        {
            continue;
        }
        if (const auto* Harvest = EM.GetComponent<HarvestComponent>(Eid))
        {
            const int ResourceID = Harvest->DesiredResourceID >= 0
                ? Harvest->DesiredResourceID
                : Harvest->TargetResourceID;
            const auto* Resource = EM.GetComponent<ResourceComponent>(ResourceID);
            if (Resource && Resource->Type == ResourceType::VespeneGas)
                continue;
        }
        if (BuildingSystem::CanBuild(
                EM, Eid, BuildingDefID, BuildTarget{}, nullptr, false))
        {
            return Eid;
        }
    }
    return -1;
}

bool AIBrain::StartGasBuilding(Core::ECS::EntityManager& EM, int BuilderID,
                              int GasBuildingDefID)
{
    int BestGeyser = -1;
    float BestDistance = 1e30f;
    for (int Eid : EM.GetEntitiesWith<TransformComponent, ResourceComponent>())
    {
        auto* T = EM.GetComponent<TransformComponent>(Eid);
        auto* R = EM.GetComponent<ResourceComponent>(Eid);
        auto* B = EM.GetComponent<BuildingComponent>(Eid);
        if (!T || !R || R->Type != ResourceType::VespeneGas ||
            R->IsDepleted || R->Amount <= 0)
        {
            continue;
        }
        if (B && B->BuildingDefID >= 0)
            continue;
        const float Distance = T->Position.DistanceTo(mAiBasePos);
        if (Distance < BestDistance)
        {
            BestDistance = Distance;
            BestGeyser = Eid;
        }
    }
    return BestGeyser >= 0 && BuildingSystem::StartBuild(
        EM, BuilderID, GasBuildingDefID, BuildTarget::FromEntity(BestGeyser));
}

bool AIBrain::StartZergGasBuilding(Core::ECS::EntityManager& EM, int DroneID,
                                   int ExtractorDefID)
{
    int BestGeyser = -1;
    float BestDistance = 1e30f;
    for (int Eid : EM.GetEntitiesWith<TransformComponent, ResourceComponent>())
    {
        auto* Transform = EM.GetComponent<TransformComponent>(Eid);
        auto* Resource = EM.GetComponent<ResourceComponent>(Eid);
        auto* Building = EM.GetComponent<BuildingComponent>(Eid);
        if (!Transform || !Resource || Resource->Type != ResourceType::VespeneGas ||
            Resource->IsDepleted || Resource->Amount <= 0)
        {
            continue;
        }
        if (Building && Building->BuildingDefID >= 0)
            continue;
        const float Distance = Transform->Position.DistanceTo(mAiBasePos);
        if (Distance < BestDistance)
        {
            BestDistance = Distance;
            BestGeyser = Eid;
        }
    }
    if (BestGeyser < 0)
        return false;

    auto* DroneUnit = EM.GetComponent<UnitComponent>(DroneID);
    const int ConsumedSupply = DroneUnit ? std::max(0, DroneUnit->SupplyCost) : 0;
    CommandSystem::InterruptHarvest(EM, DroneID);
    if (!BuildingSystem::StartBuild(
            EM, DroneID, ExtractorDefID, BuildTarget::FromEntity(BestGeyser)))
    {
        return false;
    }
    auto& Resources = HarvestSystem::GetResources(mAIPlayerID);
    Resources.SupplyUsed = std::max(0, Resources.SupplyUsed - ConsumedSupply);
    EM.DestroyEntity(DroneID);
    Core::Utils::Logger::Info(
        "AIBrain", "Ordered Extractor on geyser=" +
        std::to_string(BestGeyser) + "; drone " + std::to_string(DroneID) +
        " consumed by real Zerg build rule");
    return true;
}

void AIBrain::ManageGas(Core::ECS::EntityManager& EM, int RefineryDefID)
{
    int RefineryID = -1;
    for (int Eid : EM.GetEntitiesWith<BuildingComponent, PlayerComponent>())
    {
        auto* B = EM.GetComponent<BuildingComponent>(Eid);
        auto* P = EM.GetComponent<PlayerComponent>(Eid);
        if (B && P && P->PlayerID == mAIPlayerID &&
            B->BuildingDefID == RefineryDefID && !B->UnderConstruction)
        {
            RefineryID = Eid;
            break;
        }
    }
    if (RefineryID < 0)
        return;

    int Assigned = 0;
    for (int Eid : EM.GetEntitiesWith<HarvestComponent, PlayerComponent>())
    {
        auto* H = EM.GetComponent<HarvestComponent>(Eid);
        auto* P = EM.GetComponent<PlayerComponent>(Eid);
        if (H && P && P->PlayerID == mAIPlayerID &&
            (H->TargetResourceID == RefineryID || H->DesiredResourceID == RefineryID))
        {
            ++Assigned;
        }
    }
    while (Assigned < 3)
    {
        int Worker = -1;
        const int RaceVal = GetRace(EM);
        const int WorkerChkIDs[] = {0x07, 0x29, 0x40};
        for (int Eid : EM.GetEntitiesWith<UnitComponent, PlayerComponent>())
        {
            auto* Unit = EM.GetComponent<UnitComponent>(Eid);
            auto* Owner = EM.GetComponent<PlayerComponent>(Eid);
            auto* Harvest = EM.GetComponent<HarvestComponent>(Eid);
            if (!Unit || !Owner || Owner->PlayerID != mAIPlayerID ||
                RaceVal < 0 || RaceVal > 2 || Unit->ChkUnitID != WorkerChkIDs[RaceVal] ||
                Unit->HitPoints <= 0 || IsAssignedToConstruction(EM, Eid) ||
                IsWorkerRepairing(EM, Eid))
                continue;
            if (Harvest && (Harvest->TargetResourceID == RefineryID ||
                            Harvest->DesiredResourceID == RefineryID))
                continue;
            // Gas saturation is allowed to pull a mineral worker, exactly as
            // a player would. Waiting for an idle worker deadlocks because
            // ManageEconomy keeps all healthy workers mining.
            Worker = Eid;
            break;
        }
        if (Worker < 0 || !HarvestSystem::IssueHarvest(EM, {Worker}, RefineryID))
            break;
        ++Assigned;
        Core::Utils::Logger::Info(
            "AIBrain", "Assigned worker " + std::to_string(Worker) +
            " to gas building " + std::to_string(RefineryID));
    }
}

Core::Math::Vec2 AIBrain::FindShowcaseMapCenter(Core::ECS::EntityManager& EM) const
{
    Core::Math::Vec2 MinPos{1e30f, 1e30f};
    Core::Math::Vec2 MaxPos{-1e30f, -1e30f};
    bool Found = false;
    for (int Eid : EM.GetEntitiesWith<TransformComponent>())
    {
        auto* T = EM.GetComponent<TransformComponent>(Eid);
        if (!T)
            continue;
        MinPos.X = std::min(MinPos.X, T->Position.X);
        MinPos.Y = std::min(MinPos.Y, T->Position.Y);
        MaxPos.X = std::max(MaxPos.X, T->Position.X);
        MaxPos.Y = std::max(MaxPos.Y, T->Position.Y);
        Found = true;
    }
    return Found ? Core::Math::Vec2{(MinPos.X + MaxPos.X) * 0.5f,
                                    (MinPos.Y + MaxPos.Y) * 0.5f}
                 : mAiBasePos + Core::Math::Vec2{1024.0f, 0.0f};
}

Core::Math::Vec2 AIBrain::ShowcaseBuildPosition(Core::ECS::EntityManager& EM,
                                                 size_t LayoutIndex) const
{
    Core::Math::Vec2 Forward = FindShowcaseMapCenter(EM) - mAiBasePos;
    if (Forward.LengthSq() < 1.0f)
        Forward = {1.0f, 0.0f};
    Forward = Forward.Normalized();
    const Core::Math::Vec2 Side{-Forward.Y, Forward.X};

    // Four staggered lanes preserve unit corridors without letting later tech
    // structures drift down the ramp or into a distant expansion. Final
    // placement additionally enforces the base plateau and a 760px radius.
    const size_t Row = LayoutIndex / 4;
    const int Lane = (int)(LayoutIndex % 4);
    const float ForwardDistance = 180.0f + (float)Row * 125.0f;
    const float SideDistance = ((float)Lane - 1.5f) * 150.0f +
                               ((Row & 1u) ? 50.0f : 0.0f);
    return mAiBasePos + Forward * ForwardDistance + Side * SideDistance;
}

void AIBrain::UpdateShowcaseRallyPoints(Core::ECS::EntityManager& EM)
{
    Core::Math::Vec2 TowardCenter = FindShowcaseMapCenter(EM) - mAiBasePos;
    const float CenterDistance = TowardCenter.Length();
    if (CenterDistance < 1.0f)
        TowardCenter = {1.0f, 0.0f};
    else
        TowardCenter = TowardCenter / CenterDistance;
    const float RallyDistance = std::max(
        900.0f, std::min(1800.0f, CenterDistance * 0.90f));
    const Core::Math::Vec2 RallyPoint = mAiBasePos + TowardCenter * RallyDistance;

    for (int Eid : EM.GetEntitiesWith<BuildingComponent, PlayerComponent>())
    {
        auto* Building = EM.GetComponent<BuildingComponent>(Eid);
        auto* Player = EM.GetComponent<PlayerComponent>(Eid);
        if (!Building || !Player || Player->PlayerID != mAIPlayerID ||
            Building->UnderConstruction)
        {
            continue;
        }
        const auto Def = Game::Races::GetUnitDefByID(Building->BuildingDefID);
        if (Def.Name != "Barracks" && Def.Name != "Factory" &&
            Def.Name != "Starport" && Def.Name != "Gateway" &&
            Def.Name != "Robotics Facility" && Def.Name != "Stargate")
        {
            continue;
        }
        auto* Command = EM.GetComponent<CommandComponent>(Eid);
        if (!Command)
            Command = &EM.AddComponent<CommandComponent>(Eid);
        Command->RallyPoint = RallyPoint;
        Command->HasRallyPoint = true;
        if (std::find(mShowcaseRallyConfiguredBuildings.begin(),
                      mShowcaseRallyConfiguredBuildings.end(), Eid) ==
            mShowcaseRallyConfiguredBuildings.end())
        {
            mShowcaseRallyConfiguredBuildings.push_back(Eid);
            Core::Utils::Logger::Info(
                "AIBrain", "Showcase rally set building=" +
                std::to_string(Eid) + " name=" + Def.Name + " point=" +
                std::to_string((int)RallyPoint.X) + "," +
                std::to_string((int)RallyPoint.Y));
        }
    }
}

void AIBrain::RunTerranShowcase(Core::ECS::EntityManager& EM)
{
    auto& Res = HarvestSystem::GetResources(mAIPlayerID);
    const int ScvDefID = Game::Races::GetUnitDefID("SCV");
    const int SupplyDepotDefID = Game::Races::GetUnitDefID("SupplyDepot");
    const int RefineryDefID = Game::Races::GetUnitDefID("Refinery");

    ManageGas(EM, RefineryDefID);
    ManageEconomy(EM);
    UpdateShowcaseRallyPoints(EM);
    int QueuedScvs = 0;
    for (int Eid : EM.GetEntitiesWith<ProductionComponent, PlayerComponent>())
    {
        auto* Prod = EM.GetComponent<ProductionComponent>(Eid);
        auto* Player = EM.GetComponent<PlayerComponent>(Eid);
        if (!Prod || !Player || Player->PlayerID != mAIPlayerID)
            continue;
        QueuedScvs += (int)std::count(Prod->Queue.begin(), Prod->Queue.end(), ScvDefID);
    }
    if (CountByDefID(EM, mAIPlayerID, ScvDefID) + QueuedScvs < 12)
        TrainUnitIfPossible(EM, ScvDefID);

    // Maintain real supply headroom throughout the production phase.
    if (Res.SupplyUsed + 8 > Res.SupplyMax &&
        CountByDefID(EM, mAIPlayerID, SupplyDepotDefID, true) ==
            CountByDefID(EM, mAIPlayerID, SupplyDepotDefID))
    {
        const int Worker = FindTerranBuilderFor(EM, SupplyDepotDefID);
        const size_t DepotLayoutIndex = 18u + (size_t)CountByDefID(
            EM, mAIPlayerID, SupplyDepotDefID, true);
        if (Worker >= 0 && StartBuildingNear(
                EM, Worker, SupplyDepotDefID,
                ShowcaseBuildPosition(EM, DepotLayoutIndex)))
        {
            Core::Utils::Logger::Info("AIBrain", "Showcase ordered SupplyDepot");
            return;
        }
    }

    struct BuildingGoal { const char* Name; int TargetCount; };
    static const BuildingGoal Goals[] = {
        {"SupplyDepot", 1}, {"Refinery", 1}, {"Barracks", 1},
        {"Academy", 1}, {"EngineeringBay", 1}, {"Factory", 1},
        {"MachineShop", 1}, {"Armory", 1}, {"Starport", 1},
        {"ControlTower", 1}, {"ScienceFacility", 2}, {"CovertOps", 1},
        {"PhysicsLab", 1}, {"CommandCenter", 2}, {"ComsatStation", 1},
        {"NuclearSilo", 1}, {"Bunker", 1}, {"MissileTurret", 1}
    };

    for (size_t GoalIndex = 0; GoalIndex < std::size(Goals); ++GoalIndex)
    {
        const int DefID = Game::Races::GetUnitDefID(Goals[GoalIndex].Name);
        if (DefID < 0 || CountByDefID(EM, mAIPlayerID, DefID, true) >= Goals[GoalIndex].TargetCount)
            continue;

        const auto Def = Game::Races::GetUnitDefByID(DefID);
        int Builder = -1;
        bool Started = false;
        if (Def.IsAddon)
        {
            Builder = FindAvailableAddonParent(EM, Def.AddonOnBuildingDefID);
            Started = Builder >= 0 && StartBuildingNear(EM, Builder, DefID, mAiBasePos);
        }
        else
        {
            Builder = FindTerranBuilderFor(EM, DefID);
            if (Builder >= 0 && DefID == RefineryDefID)
                Started = StartGasBuilding(EM, Builder, DefID);
            else if (Builder >= 0)
            {
                Started = StartBuildingNear(
                    EM, Builder, DefID,
                    ShowcaseBuildPosition(EM, GoalIndex));
            }
        }
        if (Started)
        {
            Core::Utils::Logger::Info(
                "AIBrain", "Showcase ordered building " + Def.Name +
                " target=" + std::to_string(Goals[GoalIndex].TargetCount));
        }
        return; // Respect tech order and wait for prerequisites/resources/builders.
    }

    bool AllUnitsComplete = true;
    const auto& TerranDefs = Game::Races::GetTerranDefs();
    for (size_t I = 0; I < TerranDefs.size(); ++I)
    {
        const int DefID = (int)I;
        const auto& Def = TerranDefs[I];
        if (Def.IsBuilding || Def.ProducedBy < 0)
            continue;
        if (CountByDefID(EM, mAIPlayerID, DefID) > 0)
            continue;
        AllUnitsComplete = false;
        if (std::find(mShowcaseQueuedUnits.begin(), mShowcaseQueuedUnits.end(), DefID) !=
            mShowcaseQueuedUnits.end())
        {
            continue;
        }
        if (TrainUnitIfPossible(EM, DefID))
        {
            mShowcaseQueuedUnits.push_back(DefID);
            Core::Utils::Logger::Info(
                "AIBrain", "Showcase queued real production " + Def.Name);
        }
        return;
    }

    if (AllUnitsComplete && !mShowcaseCompleteLogged)
    {
        mShowcaseCompleteLogged = true;
        Core::Utils::Logger::Info(
            "AIBrain", "Showcase complete: all Terran buildings and trainable units ready");
    }
}

void AIBrain::RunProtossShowcase(Core::ECS::EntityManager& EM)
{
    auto& Res = HarvestSystem::GetResources(mAIPlayerID);
    const int ProbeDefID = Game::Races::GetUnitDefID("Probe");
    const int PylonDefID = Game::Races::GetUnitDefID("Pylon");
    const int AssimilatorDefID = Game::Races::GetUnitDefID("Assimilator");

    ManageEconomy(EM);
    UpdateShowcaseRallyPoints(EM);

    Core::Math::Vec2 Forward = FindShowcaseMapCenter(EM) - mAiBasePos;
    if (Forward.LengthSq() < 1.0f)
        Forward = {1.0f, 0.0f};
    Forward = Forward.Normalized();
    const Core::Math::Vec2 Side{-Forward.Y, Forward.X};
    const auto PylonAnchor = [&](int AnchorIndex)
    {
        static const float AnchorOffsets[5][2] = {
            {180.0f, -260.0f}, {180.0f, 260.0f},
            {430.0f, -260.0f}, {430.0f, 260.0f},
            {650.0f, 0.0f}
        };
        AnchorIndex = std::clamp(AnchorIndex, 0, 4);
        return mAiBasePos + Forward * AnchorOffsets[AnchorIndex][0] +
               Side * AnchorOffsets[AnchorIndex][1];
    };
    const auto PoweredBuildingPosition = [&](int AnchorIndex, int SlotIndex)
    {
        static const float SlotOffsets[3][2] = {
            {0.0f, -112.0f}, {0.0f, 112.0f}, {96.0f, 0.0f}
        };
        SlotIndex = std::clamp(SlotIndex, 0, 2);
        return PylonAnchor(AnchorIndex) + Forward * SlotOffsets[SlotIndex][0] +
               Side * SlotOffsets[SlotIndex][1];
    };

    // Protoss progression begins with a completed Pylon. Do not let the gas
    // manager or the general building-goal loop place anything in parallel
    // with the first power source: the original build order opens the warp
    // anchor, waits for the Pylon to finish, then starts powered structures.
    const int CompletedPylons =
        CountByDefID(EM, mAIPlayerID, PylonDefID, false);
    const int ExistingPylons =
        CountByDefID(EM, mAIPlayerID, PylonDefID, true);
    if (CompletedPylons < 1)
    {
        if (ExistingPylons < 1)
        {
            const int Probe = FindProtossBuilderFor(EM, PylonDefID);
            if (Probe >= 0 && StartBuildingNear(
                    EM, Probe, PylonDefID, PylonAnchor(0)))
            {
                Core::Utils::Logger::Info(
                    "AIBrain", "Showcase ordered first Pylon; waiting for completion");
            }
        }
        return;
    }

    ManageGas(EM, AssimilatorDefID);

    int QueuedProbes = 0;
    for (int Eid : EM.GetEntitiesWith<ProductionComponent, PlayerComponent>())
    {
        auto* Prod = EM.GetComponent<ProductionComponent>(Eid);
        auto* Player = EM.GetComponent<PlayerComponent>(Eid);
        if (!Prod || !Player || Player->PlayerID != mAIPlayerID)
            continue;
        QueuedProbes += (int)std::count(
            Prod->Queue.begin(), Prod->Queue.end(), ProbeDefID);
    }
    if (CountByDefID(EM, mAIPlayerID, ProbeDefID) + QueuedProbes < 12)
        TrainUnitIfPossible(EM, ProbeDefID);

    // Keep enough psi supply for one of every normal Protoss unit plus the
    // additional templars used by the two real Archon merges.
    if (Res.SupplyUsed + 8 > Res.SupplyMax &&
        CountByDefID(EM, mAIPlayerID, PylonDefID, true) ==
            CountByDefID(EM, mAIPlayerID, PylonDefID))
    {
        const int Probe = FindProtossBuilderFor(EM, PylonDefID);
        const int AnchorIndex = std::min(
            4, CountByDefID(EM, mAIPlayerID, PylonDefID, true));
        if (Probe >= 0 && StartBuildingNear(
                EM, Probe, PylonDefID,
                PylonAnchor(AnchorIndex)))
        {
            Core::Utils::Logger::Info("AIBrain", "Showcase ordered Pylon for supply");
            return;
        }
    }

    struct BuildingGoal
    {
        const char* Name;
        int TargetCount;
        int PylonAnchorIndex;
        int SlotIndex;
    };
    // Five separated Pylons form visible powered neighborhoods. Every powered
    // structure is placed around its neighborhood's Pylon rather than forming
    // one dense lane or drifting away from the starting plateau.
    static const BuildingGoal Goals[] = {
        {"Pylon", 1, 0, -1}, {"Assimilator", 1, 0, -1},
        {"Gateway", 1, 0, 0}, {"Forge", 1, 0, 1},
        {"Photon Cannon", 1, 0, 2}, {"Pylon", 2, 1, -1},
        {"Cybernetics Core", 1, 1, 0}, {"Shield Battery", 1, 1, 1},
        {"Robotics Facility", 1, 1, 2}, {"Pylon", 3, 2, -1},
        {"Stargate", 1, 2, 0}, {"Citadel of Adun", 1, 2, 1},
        {"Templar Archives", 1, 2, 2}, {"Pylon", 4, 3, -1},
        {"Robotics Support Bay", 1, 3, 0}, {"Fleet Beacon", 1, 3, 1},
        {"Arbiter Tribunal", 1, 3, 2}, {"Pylon", 5, 4, -1},
        {"Observatory", 1, 4, 0}, {"Nexus", 2, 4, 1}
    };

    for (const auto& Goal : Goals)
    {
        const int DefID = Game::Races::GetUnitDefID(Goal.Name);
        if (DefID < 0 ||
            CountByDefID(EM, mAIPlayerID, DefID, true) >= Goal.TargetCount)
        {
            continue;
        }

        const int Probe = FindProtossBuilderFor(EM, DefID);
        bool Started = false;
        if (Probe >= 0 && DefID == AssimilatorDefID)
            Started = StartGasBuilding(EM, Probe, DefID);
        else if (Probe >= 0)
        {
            const Core::Math::Vec2 Position = DefID == PylonDefID
                ? PylonAnchor(Goal.PylonAnchorIndex)
                : PoweredBuildingPosition(Goal.PylonAnchorIndex, Goal.SlotIndex);
            Started = StartBuildingNear(
                EM, Probe, DefID, Position);
        }
        if (Started)
        {
            const auto Def = Game::Races::GetUnitDefByID(DefID);
            Core::Utils::Logger::Info(
                "AIBrain", "Showcase ordered Protoss building " + Def.Name +
                " target=" + std::to_string(Goal.TargetCount));
        }
        return;
    }

    bool AllTrainableUnitsComplete = true;
    const auto& ProtossDefs = Game::Races::GetProtossDefs();
    const int ProtossOffset = (int)Game::Races::GetTerranDefs().size() +
                              (int)Game::Races::GetZergDefs().size();
    for (size_t I = 0; I < ProtossDefs.size(); ++I)
    {
        const int DefID = ProtossOffset + (int)I;
        const auto& Def = ProtossDefs[I];
        if (Def.IsBuilding || Def.ProducedBy < 0)
            continue;
        if (CountByDefID(EM, mAIPlayerID, DefID) > 0)
            continue;
        AllTrainableUnitsComplete = false;
        if (std::find(mShowcaseQueuedUnits.begin(), mShowcaseQueuedUnits.end(), DefID) !=
            mShowcaseQueuedUnits.end())
        {
            return;
        }
        if (TrainUnitIfPossible(EM, DefID))
        {
            mShowcaseQueuedUnits.push_back(DefID);
            Core::Utils::Logger::Info(
                "AIBrain", "Showcase queued real Protoss production " + Def.Name);
        }
        return;
    }
    if (!AllTrainableUnitsComplete)
        return;

    auto CountQueued = [&](int DefID)
    {
        int Count = 0;
        for (int Eid : EM.GetEntitiesWith<ProductionComponent, PlayerComponent>())
        {
            auto* Prod = EM.GetComponent<ProductionComponent>(Eid);
            auto* Player = EM.GetComponent<PlayerComponent>(Eid);
            if (Prod && Player && Player->PlayerID == mAIPlayerID)
                Count += (int)std::count(Prod->Queue.begin(), Prod->Queue.end(), DefID);
        }
        return Count;
    };
    auto FindOwnedUnits = [&](int DefID)
    {
        std::vector<int> Result;
        for (int Eid : EM.GetEntitiesWith<UnitComponent, PlayerComponent>())
        {
            auto* Unit = EM.GetComponent<UnitComponent>(Eid);
            auto* Player = EM.GetComponent<PlayerComponent>(Eid);
            if (Unit && Player && Player->PlayerID == mAIPlayerID &&
                Unit->HitPoints > 0 &&
                Game::Races::GetUnitDefIDByChkUnitID(Unit->ChkUnitID) == DefID)
            {
                Result.push_back(Eid);
            }
        }
        return Result;
    };

    const int HighTemplarDefID = Game::Races::GetUnitDefID("High Templar");
    const int ArchonDefID = Game::Races::GetUnitDefID("Archon");
    if (CountByDefID(EM, mAIPlayerID, ArchonDefID) == 0)
    {
        const int Total = CountByDefID(EM, mAIPlayerID, HighTemplarDefID) +
                          CountQueued(HighTemplarDefID);
        if (Total < 3)
        {
            TrainUnitIfPossible(EM, HighTemplarDefID);
            return;
        }
        auto Templars = FindOwnedUnits(HighTemplarDefID);
        if (Templars.size() < 3)
            return;
        ProtossSystem::MorphArchon(EM, Templars[0], Templars[1], false);
        return;
    }

    const int DarkTemplarDefID = Game::Races::GetUnitDefID("Dark Templar");
    const int DarkArchonDefID = Game::Races::GetUnitDefID("Dark Archon");
    if (CountByDefID(EM, mAIPlayerID, DarkArchonDefID) == 0)
    {
        const int Total = CountByDefID(EM, mAIPlayerID, DarkTemplarDefID) +
                          CountQueued(DarkTemplarDefID);
        if (Total < 3)
        {
            TrainUnitIfPossible(EM, DarkTemplarDefID);
            return;
        }
        auto Templars = FindOwnedUnits(DarkTemplarDefID);
        if (Templars.size() < 3)
            return;
        ProtossSystem::MorphArchon(EM, Templars[0], Templars[1], true);
        return;
    }

    if (!mShowcaseCompleteLogged)
    {
        mShowcaseCompleteLogged = true;
        Core::Utils::Logger::Info(
            "AIBrain", "Showcase complete: all Protoss buildings and normal units ready");
    }
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
    (void)ProducerDefID;
    int WorkerCount = CountByDefID(EM, mAIPlayerID, WorkerDefID);
    const int QueuedWorkers = CountQueuedByDefID(EM, mAIPlayerID, WorkerDefID);
    if (WorkerCount + QueuedWorkers < MaxWorkers)
        TrainUnitIfPossible(EM, WorkerDefID);
}

void AIBrain::RunTerranAI(Core::ECS::EntityManager& EM)
{
    int SvcDefID = Game::Races::GetUnitDefID("SCV");
    int MarineDefID = Game::Races::GetUnitDefID("Marine");
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
    int ScienceFacilityDefID = Game::Races::GetUnitDefID("ScienceFacility");
    int CommandCenterDefID = Game::Races::GetUnitDefID("CommandCenter");
    int RefineryDefID = Game::Races::GetUnitDefID("Refinery");

    auto& Res = HarvestSystem::GetResources(mAIPlayerID);
    int scvCount = CountByDefID(EM, mAIPlayerID, SvcDefID);
    int supplyDepotCount = CountByDefID(EM, mAIPlayerID, SupplyDepotDefID);
    int supplyDepotStartedCount =
        CountByDefID(EM, mAIPlayerID, SupplyDepotDefID, true);
    int barracksCount = CountByDefID(EM, mAIPlayerID, BarracksDefID);
    int barracksStartedCount = CountByDefID(EM, mAIPlayerID, BarracksDefID, true);
    int factoryCount = CountByDefID(EM, mAIPlayerID, FactoryDefID);
    int factoryStartedCount = CountByDefID(EM, mAIPlayerID, FactoryDefID, true);
    int starportCount = CountByDefID(EM, mAIPlayerID, StarportDefID);
    int starportStartedCount = CountByDefID(EM, mAIPlayerID, StarportDefID, true);
    int academyStartedCount = CountByDefID(EM, mAIPlayerID, AcademyDefID, true);
    int refineryStartedCount = CountByDefID(EM, mAIPlayerID, RefineryDefID, true);
    int commandCenterStartedCount = CountByDefID(
        EM, mAIPlayerID, CommandCenterDefID, true);

    int marineCount = CountByDefID(EM, mAIPlayerID, MarineDefID);
    int tankCount = CountByDefID(EM, mAIPlayerID, TankDefID);
    int goliathCount = CountByDefID(EM, mAIPlayerID, GoliathDefID);
    int wraithCount = CountByDefID(EM, mAIPlayerID, WraithDefID);

    // Stage progression
    if (mStage < 1 && scvCount >= 12 && barracksCount >= 2) mStage = 1;
    if (mStage < 2 && factoryCount >= 1 && tankCount >= 4) mStage = 2;
    if (mStage < 3 && starportCount >= 1 && wraithCount >= 2) mStage = 3;

    // Always: workers + supply
    TrainWorkerIfNeeded(EM, CommandCenterDefID, SvcDefID, 20);
    if (scvCount >= 8 &&
        Res.SupplyUsed + 4 >= Res.SupplyMax &&
        supplyDepotStartedCount <= supplyDepotCount &&
        Res.Minerals >= 100)
    {
        int idleSCV = FindTerranBuilderFor(EM, SupplyDepotDefID);
        if (idleSCV >= 0)
            StartBuildingNear(EM, idleSCV, SupplyDepotDefID,
                Core::Math::Vec2(mAiBasePos.X + 60.0f * (float)(supplyDepotCount + 1),
                                 mAiBasePos.Y));
    }

    if (scvCount >= 8 && refineryStartedCount < 1 && Res.Minerals >= 100)
    {
        const int Builder = FindTerranBuilderFor(EM, RefineryDefID);
        if (Builder >= 0)
            StartGasBuilding(EM, Builder, RefineryDefID);
    }

    // Stage 0: Barracks + Academy
    if (mStage < 1)
    {
        const int DesiredBarracks = marineCount >= 6 ? 2 : 1;
        if (barracksStartedCount < DesiredBarracks && Res.Minerals >= 150)
        {
            int idleSCV = FindTerranBuilderFor(EM, BarracksDefID);
            if (idleSCV >= 0)
                StartBuildingNear(EM, idleSCV, BarracksDefID,
                    Core::Math::Vec2(mAiBasePos.X + 100.0f, mAiBasePos.Y + 80.0f * (float)(barracksCount + 1)));
        }
        if (academyStartedCount < 1 && barracksCount >= 1 &&
            Res.Minerals >= 150)
        {
            int idleSCV = FindTerranBuilderFor(EM, AcademyDefID);
            if (idleSCV >= 0)
                StartBuildingNear(EM, idleSCV, AcademyDefID,
                    Core::Math::Vec2(mAiBasePos.X + 180.0f, mAiBasePos.Y));
        }
    }

    // Easy melee keeps its basic attack group alive across tech stages.  In
    // particular, entering Stage 1 must not strand the army below the first
    // eight-unit attack threshold while gas technology is still pending.
    if (marineCount + CountQueuedByDefID(EM, mAIPlayerID, MarineDefID) < 12)
        TrainUnitIfPossible(EM, MarineDefID);

    // Stage 1: Factory + Machine Shop
    if (mStage >= 1)
    {
        if (factoryStartedCount < 1 && Res.Minerals >= 200 && Res.VespeneGas >= 100)
        {
            int idleSCV = FindTerranBuilderFor(EM, FactoryDefID);
            if (idleSCV >= 0)
                StartBuildingNear(EM, idleSCV, FactoryDefID,
                    Core::Math::Vec2(mAiBasePos.X - 100.0f, mAiBasePos.Y));
        }
        if (factoryCount >= 1 &&
            CountByDefID(EM, mAIPlayerID, MachineShopDefID, true) < 1 &&
            Res.Minerals >= 50)
        {
            int factory = FindNearestFreeBuilding(EM, FactoryDefID);
            if (factory >= 0)
                StartBuildingNear(EM, factory, MachineShopDefID,
                    Core::Math::Vec2(mAiBasePos.X - 120.0f, mAiBasePos.Y));
        }
        // Tanks + Goliaths
        if (tankCount + CountQueuedByDefID(EM, mAIPlayerID, TankDefID) < 4)
            TrainUnitIfPossible(EM, TankDefID);
        if (goliathCount + CountQueuedByDefID(EM, mAIPlayerID, GoliathDefID) < 3)
            TrainUnitIfPossible(EM, GoliathDefID);
    }

    // Stage 2: Starport + Wraiths
    if (mStage >= 2)
    {
        if (starportStartedCount < 1 && Res.Minerals >= 150 && Res.VespeneGas >= 100)
        {
            int idleSCV = FindTerranBuilderFor(EM, StarportDefID);
            if (idleSCV >= 0)
                StartBuildingNear(EM, idleSCV, StarportDefID,
                    Core::Math::Vec2(mAiBasePos.X - 160.0f, mAiBasePos.Y - 80.0f));
        }
        if (wraithCount + CountQueuedByDefID(EM, mAIPlayerID, WraithDefID) < 4)
            TrainUnitIfPossible(EM, WraithDefID);
    }

    // Stage 3: Science Facility + Battlecruisers
    if (mStage >= 3)
    {
        if (CountByDefID(EM, mAIPlayerID, ScienceFacilityDefID, true) < 1 &&
            Res.Minerals >= 100 && Res.VespeneGas >= 150)
        {
            int idleSCV = FindTerranBuilderFor(EM, ScienceFacilityDefID);
            if (idleSCV >= 0)
                StartBuildingNear(EM, idleSCV, ScienceFacilityDefID,
                    Core::Math::Vec2(mAiBasePos.X - 200.0f, mAiBasePos.Y - 160.0f));
        }
        if (CountByDefID(EM, mAIPlayerID, BattlecruiserDefID) +
                CountQueuedByDefID(EM, mAIPlayerID, BattlecruiserDefID) < 2)
            TrainUnitIfPossible(EM, BattlecruiserDefID);
    }

    if (!mExpansionStarted && mElapsedSeconds >= 480.0f && scvCount >= 18 &&
        marineCount + tankCount + goliathCount >= 12 &&
        commandCenterStartedCount < 2 && Res.Minerals >= 400)
    {
        const int Builder = FindTerranBuilderFor(EM, CommandCenterDefID);
        if (Builder >= 0 && StartBuildingNear(
                EM, Builder, CommandCenterDefID, mExpansionPos))
        {
            mExpansionStarted = true;
            Core::Utils::Logger::Info(
                "AIBrain", "Player " + std::to_string(mAIPlayerID) +
                " easy expansion ordered race=Terran");
        }
    }

    ManageGas(EM, RefineryDefID);

    // Send harvesters to work
    ManageEconomy(EM);

    // Attack wave
    int totalCombat = marineCount + tankCount + goliathCount + wraithCount;
    if ((totalCombat >= 8 && !mDefendingBase) || mElapsedSeconds >= 2700.0f)
        SendAttackWave(EM, 8);
}

void AIBrain::RunZergShowcase(Core::ECS::EntityManager& EM)
{
    auto& Resources = HarvestSystem::GetResources(mAIPlayerID);
    const int DroneDefID = Game::Races::GetUnitDefID("Drone");
    const int OverlordDefID = Game::Races::GetUnitDefID("Overlord");
    const int HatcheryDefID = Game::Races::GetUnitDefID("Hatchery");
    const int LairDefID = Game::Races::GetUnitDefID("Lair");
    const int HiveDefID = Game::Races::GetUnitDefID("Hive");
    const int SpireDefID = Game::Races::GetUnitDefID("Spire");
    const int GreaterSpireDefID = Game::Races::GetUnitDefID("Greater Spire");
    const int ExtractorDefID = Game::Races::GetUnitDefID("Extractor");

    const auto CountPendingMorphs = [&](int TargetDefID)
    {
        int Count = 0;
        for (int Eid : EM.GetEntitiesWith<MorphComponent, PlayerComponent>())
        {
            auto* Morph = EM.GetComponent<MorphComponent>(Eid);
            auto* Player = EM.GetComponent<PlayerComponent>(Eid);
            if (Morph && Player && Player->PlayerID == mAIPlayerID &&
                Morph->IsMorphing && Morph->MorphTargetDefID == TargetDefID)
            {
                ++Count;
            }
        }
        return Count;
    };
    const auto FindIdleLarva = [&]()
    {
        for (int Eid : EM.GetEntitiesWith<UnitComponent, PlayerComponent>())
        {
            auto* Unit = EM.GetComponent<UnitComponent>(Eid);
            auto* Player = EM.GetComponent<PlayerComponent>(Eid);
            auto* Morph = EM.GetComponent<MorphComponent>(Eid);
            if (Unit && Player && Player->PlayerID == mAIPlayerID &&
                Unit->ChkUnitID == 0x23 && Unit->HitPoints > 0 &&
                (!Morph || !Morph->IsMorphing))
            {
                return Eid;
            }
        }
        return -1;
    };
    const auto MorphLarva = [&](int TargetDefID, const char* Label)
    {
        const int LarvaID = FindIdleLarva();
        if (LarvaID < 0 ||
            !ZergSystem::StartMorphLarvaIntoUnit(EM, LarvaID, TargetDefID))
        {
            return false;
        }
        Core::Utils::Logger::Info(
            "AIBrain", "Showcase ordered real larva morph: " +
            std::string(Label));
        return true;
    };
    const auto MorphOwnedBuilding = [&](int SourceDefID, int TargetDefID)
    {
        for (int Eid : EM.GetEntitiesWith<BuildingComponent, PlayerComponent>())
        {
            auto* Building = EM.GetComponent<BuildingComponent>(Eid);
            auto* Player = EM.GetComponent<PlayerComponent>(Eid);
            if (!Building || !Player || Player->PlayerID != mAIPlayerID ||
                Building->BuildingDefID != SourceDefID || Building->UnderConstruction)
            {
                continue;
            }
            if (ZergSystem::StartMorphBuilding(EM, Eid, TargetDefID))
            {
                Core::Utils::Logger::Info(
                    "AIBrain", "Showcase ordered real building morph " +
                    Game::Races::GetUnitDefByID(SourceDefID).Name + " -> " +
                    Game::Races::GetUnitDefByID(TargetDefID).Name);
                return true;
            }
        }
        return false;
    };
    const auto RunBuildingGoals = [&](const auto& Goals, size_t LayoutOffset)
    {
        for (size_t GoalIndex = 0; GoalIndex < std::size(Goals); ++GoalIndex)
        {
            const int DefID = Game::Races::GetUnitDefID(Goals[GoalIndex].Name);
            if (DefID < 0 || CountByDefID(
                    EM, mAIPlayerID, DefID, true) >= Goals[GoalIndex].TargetCount)
            {
                continue;
            }
            const int DroneID = FindZergBuilderFor(EM, DefID);
            bool Started = false;
            if (DroneID >= 0 && DefID == ExtractorDefID)
                Started = StartZergGasBuilding(EM, DroneID, DefID);
            else if (DroneID >= 0)
            {
                Started = StartZergBuildingNear(
                    EM, DroneID, DefID,
                    ShowcaseBuildPosition(EM, LayoutOffset + GoalIndex));
            }
            if (Started)
            {
                Core::Utils::Logger::Info(
                    "AIBrain", "Showcase ordered Zerg building " +
                    Game::Races::GetUnitDefByID(DefID).Name +
                    "; one Drone consumed");
            }
            return false;
        }
        return true;
    };

    ManageGas(EM, ExtractorDefID);
    ManageEconomy(EM);

    // Supply must be resolved before worker growth. Otherwise a capped Drone
    // morph fails every decision tick and prevents the Brain from ever
    // reaching its Overlord rule.
    if (Resources.SupplyUsed + 4 > Resources.SupplyMax &&
        CountPendingMorphs(OverlordDefID) == 0)
    {
        MorphLarva(OverlordDefID, "Overlord");
        return;
    }

    // Every ordinary structure removes one live Drone. Keep the workforce
    // replacement in the same real larva/supply/resource pipeline so the
    // visible count falls on build start and recovers only after a new egg
    // completes.
    constexpr int DesiredDroneCount = 16;
    const int ActiveDrones = CountByDefID(EM, mAIPlayerID, DroneDefID);
    const int PendingDrones = CountPendingMorphs(DroneDefID);
    if (ActiveDrones + PendingDrones < DesiredDroneCount)
    {
        MorphLarva(DroneDefID, "Drone");
        return;
    }

    struct BuildingGoal { const char* Name; int TargetCount; };
    static const BuildingGoal EarlyGoals[] = {
        {"Extractor", 1}, {"Spawning Pool", 1},
        {"Evolution Chamber", 1}, {"Hatchery", 2}
    };
    if (!RunBuildingGoals(EarlyGoals, 0u))
        return;

    if (CountByDefID(EM, mAIPlayerID, LairDefID) < 1)
    {
        MorphOwnedBuilding(HatcheryDefID, LairDefID);
        return;
    }

    static const BuildingGoal LairGoals[] = {
        {"Hydralisk Den", 1}, {"Spire", 1}, {"Queen's Nest", 1},
        {"Nydus Canal", 1}
    };
    if (!RunBuildingGoals(LairGoals, 6u))
        return;

    if (CountByDefID(EM, mAIPlayerID, HiveDefID) < 1)
    {
        MorphOwnedBuilding(LairDefID, HiveDefID);
        return;
    }

    static const BuildingGoal HiveGoals[] = {
        {"Ultralisk Cavern", 1}, {"Defiler Mound", 1}
    };
    if (!RunBuildingGoals(HiveGoals, 12u))
        return;

    if (CountByDefID(EM, mAIPlayerID, GreaterSpireDefID) < 1)
    {
        MorphOwnedBuilding(SpireDefID, GreaterSpireDefID);
        return;
    }

    static const char* UnitGoals[] = {
        "Zergling", "Hydralisk", "Mutalisk", "Scourge",
        "Queen", "Ultralisk", "Defiler", "Lurker"
    };
    for (const char* Name : UnitGoals)
    {
        const int DefID = Game::Races::GetUnitDefID(Name);
        if (CountByDefID(EM, mAIPlayerID, DefID) > 0)
            continue;
        if (CountPendingMorphs(DefID) == 0)
            MorphLarva(DefID, Name);
        return;
    }

    if (!mShowcaseCompleteLogged)
    {
        mShowcaseCompleteLogged = true;
        Core::Utils::Logger::Info(
            "AIBrain",
            "Showcase complete: Zerg tech tree and normal larva units ready; "
            "all buildings used real Drone consumption");
    }
}

void AIBrain::RunZergAI(Core::ECS::EntityManager& EM)
{
    int DroneDefID = Game::Races::GetUnitDefID("Drone");
    int OverlordDefID = Game::Races::GetUnitDefID("Overlord");
    int ZerglingDefID = Game::Races::GetUnitDefID("Zergling");
    int HydraliskDefID = Game::Races::GetUnitDefID("Hydralisk");
    int MutaliskDefID = Game::Races::GetUnitDefID("Mutalisk");
    int UltraliskDefID = Game::Races::GetUnitDefID("Ultralisk");
    int HatcheryDefID = Game::Races::GetUnitDefID("Hatchery");
    int LairDefID = Game::Races::GetUnitDefID("Lair");
    int HiveDefID = Game::Races::GetUnitDefID("Hive");
    int SpawningPoolDefID = Game::Races::GetUnitDefID("Spawning Pool");
    int HydraliskDenDefID = Game::Races::GetUnitDefID("Hydralisk Den");
    int SpireDefID = Game::Races::GetUnitDefID("Spire");
    int UltraliskCavernDefID = Game::Races::GetUnitDefID("Ultralisk Cavern");
    int ExtractorDefID = Game::Races::GetUnitDefID("Extractor");

    auto& Res = HarvestSystem::GetResources(mAIPlayerID);
    int droneCount = CountByDefID(EM, mAIPlayerID, DroneDefID);
    int hatcheryCount = CountByDefID(EM, mAIPlayerID, HatcheryDefID);
    int lairCount = CountByDefID(EM, mAIPlayerID, LairDefID);
    int poolCount = CountByDefID(EM, mAIPlayerID, SpawningPoolDefID);
    int poolStartedCount = CountByDefID(
        EM, mAIPlayerID, SpawningPoolDefID, true);
    int denCount = CountByDefID(EM, mAIPlayerID, HydraliskDenDefID);
    int spireCount = CountByDefID(EM, mAIPlayerID, SpireDefID);
    int zerglingCount = CountByDefID(EM, mAIPlayerID, ZerglingDefID);
    int hydraCount = CountByDefID(EM, mAIPlayerID, HydraliskDefID);
    int mutaCount = CountByDefID(EM, mAIPlayerID, MutaliskDefID);
    int ultraCount = CountByDefID(EM, mAIPlayerID, UltraliskDefID);
    int extractorStartedCount = CountByDefID(
        EM, mAIPlayerID, ExtractorDefID, true);

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

    auto CountMorphingInto = [&](int DefID)
    {
        int Count = 0;
        for (int EntityID : EM.GetEntitiesWith<MorphComponent, PlayerComponent>())
        {
            auto* Morph = EM.GetComponent<MorphComponent>(EntityID);
            auto* Player = EM.GetComponent<PlayerComponent>(EntityID);
            if (Morph && Player && Player->PlayerID == mAIPlayerID &&
                Morph->IsMorphing && Morph->MorphTargetDefID == DefID)
            {
                ++Count;
            }
        }
        return Count;
    };

    // Stage progression
    if (mStage < 1 && droneCount >= 10 && poolCount >= 1 && zerglingCount >= 6)
        mStage = 1;
    if (mStage < 2 && lairCount >= 1 && denCount >= 1 && mutaCount >= 4) mStage = 2;
    if (mStage < 3 && CountByDefID(EM, mAIPlayerID, HiveDefID) >= 1 && ultraCount >= 2) mStage = 3;

    // Supply: morph overlords
    if (Res.SupplyUsed + 2 >= Res.SupplyMax && Res.Minerals >= 100)
    {
        int larva = FindIdleLarva();
        if (larva >= 0)
            ZergSystem::StartMorphLarvaIntoUnit(EM, larva, OverlordDefID);
    }

    if (droneCount >= 9 && extractorStartedCount < 1 && Res.Minerals >= 50)
    {
        const int Drone = FindIdleWorker(EM);
        if (Drone >= 0)
            StartZergGasBuilding(EM, Drone, ExtractorDefID);
    }

    if (mStage >= 1 &&
        droneCount + CountMorphingInto(DroneDefID) < 20 && Res.Minerals >= 50)
    {
        const int Larva = FindIdleLarva();
        if (Larva >= 0)
            ZergSystem::StartMorphLarvaIntoUnit(EM, Larva, DroneDefID);
    }

    // Stage 0: Drones + Spawning Pool + Zerglings
    if (mStage < 1)
    {
        // Drones up to 12
        if (droneCount + CountMorphingInto(DroneDefID) < 12 && Res.Minerals >= 50)
        {
            int larva = FindIdleLarva();
            if (larva >= 0)
                ZergSystem::StartMorphLarvaIntoUnit(EM, larva, DroneDefID);
        }

        // Spawning Pool
        if (poolStartedCount < 1 && droneCount >= 6 && Res.Minerals >= 200)
        {
            int drone = FindIdleWorker(EM);
            if (drone >= 0)
                StartZergBuildingNear(EM, drone, SpawningPoolDefID,
                    Core::Math::Vec2(mAiBasePos.X + 100.0f, mAiBasePos.Y));
        }

        // Zerglings
        if (poolCount >= 1 &&
            zerglingCount + CountMorphingInto(ZerglingDefID) < 12 &&
            Res.Minerals >= 50)
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
        if (CountByDefID(EM, mAIPlayerID, HydraliskDenDefID, true) < 1 &&
            lairCount >= 1 && Res.Minerals >= 75 && Res.VespeneGas >= 50)
        {
            int drone = FindIdleWorker(EM);
            if (drone >= 0)
                StartZergBuildingNear(EM, drone, HydraliskDenDefID,
                    Core::Math::Vec2(mAiBasePos.X - 100.0f, mAiBasePos.Y));
        }

        // Hydralisks
        if (denCount >= 1 &&
            hydraCount + CountMorphingInto(HydraliskDefID) < 8 &&
            Res.Minerals >= 75 && Res.VespeneGas >= 25)
        {
            int larva = FindIdleLarva();
            if (larva >= 0)
                ZergSystem::StartMorphLarvaIntoUnit(EM, larva, HydraliskDefID);
        }

        // Spire
        if (CountByDefID(EM, mAIPlayerID, SpireDefID, true) < 1 &&
            lairCount >= 1 && Res.Minerals >= 100 && Res.VespeneGas >= 100)
        {
            int drone = FindIdleWorker(EM);
            if (drone >= 0)
                StartZergBuildingNear(EM, drone, SpireDefID,
                    Core::Math::Vec2(mAiBasePos.X - 140.0f, mAiBasePos.Y));
        }

        // Mutalisks
        if (spireCount >= 1 &&
            mutaCount + CountMorphingInto(MutaliskDefID) < 6 &&
            Res.Minerals >= 100 && Res.VespeneGas >= 100)
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

        if (CountByDefID(EM, mAIPlayerID, UltraliskCavernDefID, true) < 1 &&
            CountByDefID(EM, mAIPlayerID, HiveDefID) >= 1 &&
            Res.Minerals >= 150 && Res.VespeneGas >= 200)
        {
            int drone = FindIdleWorker(EM);
            if (drone >= 0)
                StartZergBuildingNear(EM, drone, UltraliskCavernDefID,
                    Core::Math::Vec2(mAiBasePos.X - 180.0f, mAiBasePos.Y - 80.0f));
        }

        if (ultraCount + CountMorphingInto(UltraliskDefID) < 4 &&
            CountByDefID(EM, mAIPlayerID, UltraliskCavernDefID) >= 1 &&
            Res.Minerals >= 200 && Res.VespeneGas >= 200)
        {
            int larva = FindIdleLarva();
            if (larva >= 0)
                ZergSystem::StartMorphLarvaIntoUnit(EM, larva, UltraliskDefID);
        }
    }

    // Extra hatcheries for larva production
    if (!mExpansionStarted && mElapsedSeconds >= 420.0f && droneCount >= 18 &&
        zerglingCount + hydraCount >= 12 && totalHatcheries < 2 &&
        Res.Minerals >= 300)
    {
        int drone = FindIdleWorker(EM);
        if (drone >= 0 && StartZergBuildingNear(
                EM, drone, HatcheryDefID, mExpansionPos))
        {
            mExpansionStarted = true;
            Core::Utils::Logger::Info(
                "AIBrain", "Player " + std::to_string(mAIPlayerID) +
                " easy expansion ordered race=Zerg");
        }
    }

    ManageGas(EM, ExtractorDefID);
    ManageEconomy(EM);

    int totalCombat = zerglingCount + hydraCount + mutaCount + ultraCount;
    if ((totalCombat >= 10 && !mDefendingBase) || mElapsedSeconds >= 2700.0f)
        SendAttackWave(EM, 12);
}

void AIBrain::RunProtossAI(Core::ECS::EntityManager& EM)
{
    int ProbeDefID = Game::Races::GetUnitDefID("Probe");
    int ZealotDefID = Game::Races::GetUnitDefID("Zealot");
    int DragoonDefID = Game::Races::GetUnitDefID("Dragoon");
    int ScoutDefID = Game::Races::GetUnitDefID("Scout");
    int CarrierDefID = Game::Races::GetUnitDefID("Carrier");
    int NexusDefID = Game::Races::GetUnitDefID("Nexus");
    int PylonDefID = Game::Races::GetUnitDefID("Pylon");
    int GatewayDefID = Game::Races::GetUnitDefID("Gateway");
    int CyberneticsCoreDefID = Game::Races::GetUnitDefID("Cybernetics Core");
    int CitadelOfAdunDefID = Game::Races::GetUnitDefID("Citadel of Adun");
    int RoboticsFacilityDefID = Game::Races::GetUnitDefID("Robotics Facility");
    int StargateDefID = Game::Races::GetUnitDefID("Stargate");
    int FleetBeaconDefID = Game::Races::GetUnitDefID("Fleet Beacon");
    int AssimilatorDefID = Game::Races::GetUnitDefID("Assimilator");

    auto& Res = HarvestSystem::GetResources(mAIPlayerID);
    int probeCount = CountByDefID(EM, mAIPlayerID, ProbeDefID);
    int pylonCount = CountByDefID(EM, mAIPlayerID, PylonDefID);
    int pylonStartedCount = CountByDefID(EM, mAIPlayerID, PylonDefID, true);
    int gatewayCount = CountByDefID(EM, mAIPlayerID, GatewayDefID);
    int gatewayStartedCount = CountByDefID(EM, mAIPlayerID, GatewayDefID, true);
    int coreCount = CountByDefID(EM, mAIPlayerID, CyberneticsCoreDefID);
    int coreStartedCount = CountByDefID(
        EM, mAIPlayerID, CyberneticsCoreDefID, true);
    int stargateCount = CountByDefID(EM, mAIPlayerID, StargateDefID);
    int zealotCount = CountByDefID(EM, mAIPlayerID, ZealotDefID);
    int dragoonCount = CountByDefID(EM, mAIPlayerID, DragoonDefID);
    int scoutCount = CountByDefID(EM, mAIPlayerID, ScoutDefID);
    int carrierCount = CountByDefID(EM, mAIPlayerID, CarrierDefID);
    int assimilatorStartedCount = CountByDefID(
        EM, mAIPlayerID, AssimilatorDefID, true);
    int nexusStartedCount = CountByDefID(EM, mAIPlayerID, NexusDefID, true);

    // Stage progression
    if (mStage < 1 && gatewayCount >= 1 && zealotCount >= 4) mStage = 1;
    if (mStage < 2 && coreCount >= 1 && dragoonCount >= 4) mStage = 2;
    if (mStage < 3 && stargateCount >= 1 && carrierCount >= 1) mStage = 3;

    // Probes from Nexus
    TrainWorkerIfNeeded(EM, NexusDefID, ProbeDefID, 20);

    // Pylons for power and supply
    if (probeCount >= 6 &&
        (pylonCount < 1 || Res.SupplyUsed + 4 >= Res.SupplyMax) &&
        pylonStartedCount <= pylonCount &&
        Res.Minerals >= 100)
    {
        int probe = FindIdleWorker(EM);
        if (probe >= 0)
            StartBuildingNear(EM, probe, PylonDefID,
                Core::Math::Vec2(mAiBasePos.X + 60.0f * (float)(pylonCount),
                                 mAiBasePos.Y + 80.0f));
    }

    if (probeCount >= 8 && assimilatorStartedCount < 1 && Res.Minerals >= 100)
    {
        const int Probe = FindIdleWorker(EM);
        if (Probe >= 0)
            StartGasBuilding(EM, Probe, AssimilatorDefID);
    }

    // Stage 0: Gateway + Zealots
    if (mStage < 1)
    {
        const int DesiredGateways = zealotCount >= 4 ? 2 : 1;
        if (gatewayStartedCount < DesiredGateways && pylonCount >= 1 &&
            Res.Minerals >= 150)
        {
            int probe = FindIdleWorker(EM);
            if (probe >= 0)
                StartBuildingNear(EM, probe, GatewayDefID,
                    Core::Math::Vec2(mAiBasePos.X + 100.0f,
                                     mAiBasePos.Y + 80.0f * (float)(gatewayCount + 1)));
        }
        if (zealotCount + CountQueuedByDefID(EM, mAIPlayerID, ZealotDefID) < 8)
            TrainUnitIfPossible(EM, ZealotDefID);
    }

    // Stage 1: Cybernetics Core + Dragoons + Citadel
    if (mStage >= 1)
    {
        if (gatewayStartedCount < 2 && pylonCount >= 1 && Res.Minerals >= 150)
        {
            const int Probe = FindIdleWorker(EM);
            if (Probe >= 0)
                StartBuildingNear(
                    EM, Probe, GatewayDefID,
                    Core::Math::Vec2(mAiBasePos.X + 100.0f,
                                     mAiBasePos.Y + 80.0f *
                                         (float)(gatewayStartedCount + 1)));
        }
        if (coreStartedCount < 1 && gatewayCount >= 1 && Res.Minerals >= 200)
        {
            int probe = FindIdleWorker(EM);
            if (probe >= 0)
                StartBuildingNear(EM, probe, CyberneticsCoreDefID,
                    Core::Math::Vec2(mAiBasePos.X - 100.0f, mAiBasePos.Y));
        }
        if (dragoonCount + CountQueuedByDefID(EM, mAIPlayerID, DragoonDefID) < 6 &&
            coreCount >= 1)
            TrainUnitIfPossible(EM, DragoonDefID);

        if (CountByDefID(EM, mAIPlayerID, CitadelOfAdunDefID, true) < 1 &&
            coreCount >= 1 && Res.Minerals >= 150 && Res.VespeneGas >= 100)
        {
            int probe = FindIdleWorker(EM);
            if (probe >= 0)
                StartBuildingNear(EM, probe, CitadelOfAdunDefID,
                    Core::Math::Vec2(mAiBasePos.X - 140.0f, mAiBasePos.Y));
        }
    }

    // Stage 2: Stargate + Scouts + Robotics
    if (mStage >= 2)
    {
        if (CountByDefID(EM, mAIPlayerID, StargateDefID, true) < 1 &&
            coreCount >= 1 && Res.Minerals >= 150 && Res.VespeneGas >= 150)
        {
            int probe = FindIdleWorker(EM);
            if (probe >= 0)
                StartBuildingNear(EM, probe, StargateDefID,
                    Core::Math::Vec2(mAiBasePos.X - 180.0f, mAiBasePos.Y + 80.0f));
        }
        if (scoutCount + CountQueuedByDefID(EM, mAIPlayerID, ScoutDefID) < 4 &&
            stargateCount >= 1)
            TrainUnitIfPossible(EM, ScoutDefID);

        if (CountByDefID(EM, mAIPlayerID, FleetBeaconDefID, true) < 1 && stargateCount >= 1 &&
            Res.Minerals >= 200 && Res.VespeneGas >= 150)
        {
            int probe = FindIdleWorker(EM);
            if (probe >= 0)
                StartBuildingNear(EM, probe, FleetBeaconDefID,
                    Core::Math::Vec2(mAiBasePos.X - 220.0f, mAiBasePos.Y));
        }
    }

    // Stage 3: Carriers + Robotics
    if (mStage >= 3)
    {
        if (carrierCount + CountQueuedByDefID(EM, mAIPlayerID, CarrierDefID) < 2 &&
            CountByDefID(EM, mAIPlayerID, FleetBeaconDefID) >= 1)
            TrainUnitIfPossible(EM, CarrierDefID);

        if (CountByDefID(EM, mAIPlayerID, RoboticsFacilityDefID, true) < 1 &&
            coreCount >= 1 && Res.Minerals >= 200 && Res.VespeneGas >= 100)
        {
            int probe = FindIdleWorker(EM);
            if (probe >= 0)
                StartBuildingNear(EM, probe, RoboticsFacilityDefID,
                    Core::Math::Vec2(mAiBasePos.X - 260.0f, mAiBasePos.Y));
        }
    }

    if (!mExpansionStarted && mElapsedSeconds >= 480.0f && probeCount >= 18 &&
        zealotCount + dragoonCount >= 8 && nexusStartedCount < 2 &&
        Res.Minerals >= 400)
    {
        const int Probe = FindIdleWorker(EM);
        if (Probe >= 0 && StartBuildingNear(
                EM, Probe, NexusDefID, mExpansionPos))
        {
            mExpansionStarted = true;
            Core::Utils::Logger::Info(
                "AIBrain", "Player " + std::to_string(mAIPlayerID) +
                " easy expansion ordered race=Protoss");
        }
    }

    ManageGas(EM, AssimilatorDefID);
    ManageEconomy(EM);

    int totalCombat = zealotCount + dragoonCount + scoutCount + carrierCount;
    if ((totalCombat >= 8 && !mDefendingBase) || mElapsedSeconds >= 2700.0f)
        SendAttackWave(EM, 8);
}

void AIBrain::ManageEconomy(Core::ECS::EntityManager& EM)
{
    std::vector<int> ActiveMinerals;
    auto Minerals = EM.GetEntitiesWith<TransformComponent, ResourceComponent>();
    for (int Mid : Minerals)
    {
        auto* Res = EM.GetComponent<ResourceComponent>(Mid);
        if (Res && Res->Type == ResourceType::Minerals &&
            !Res->IsDepleted && Res->Amount > 0)
            ActiveMinerals.push_back(Mid);
    }
    if (ActiveMinerals.empty()) return;

    const int RaceVal = GetRace(EM);
    const int WorkerChkIDs[] = {0x07, 0x29, 0x40}; // SCV, Drone, Probe
    auto Units = EM.GetEntitiesWith<UnitComponent, PlayerComponent>();
    for (int Eid : Units)
    {
        auto* U = EM.GetComponent<UnitComponent>(Eid);
        auto* P = EM.GetComponent<PlayerComponent>(Eid);
        if (!U || !P) continue;
        if (P->PlayerID != mAIPlayerID) continue;
        if (RaceVal < 0 || RaceVal > 2 || U->ChkUnitID != WorkerChkIDs[RaceVal]) continue;
        if (IsAssignedToConstruction(EM, Eid)) continue;
        if (IsWorkerRepairing(EM, Eid)) continue;
        auto* H = EM.GetComponent<HarvestComponent>(Eid);
        if (!H)
            H = &EM.AddComponent<HarvestComponent>(Eid);
        // Never steal a worker from a live owned gas building.
        const int AssignedResourceID = H->DesiredResourceID >= 0
            ? H->DesiredResourceID : H->TargetResourceID;
        auto* CurrentResource = EM.GetComponent<ResourceComponent>(AssignedResourceID);
        if (CurrentResource && CurrentResource->Type == ResourceType::VespeneGas)
            continue;

        const bool NeedsAssignment = H->State == HarvestState::Idle ||
            !CurrentResource || CurrentResource->Type != ResourceType::Minerals ||
            CurrentResource->IsDepleted || CurrentResource->Amount <= 0;
        if (!NeedsAssignment)
            continue;

        int BestMineral = -1;
        size_t BestLoad = std::numeric_limits<size_t>::max();
        float BestDistance = std::numeric_limits<float>::max();
        const auto* WorkerPos = EM.GetComponent<TransformComponent>(Eid);
        for (int MineralID : ActiveMinerals)
        {
            auto* Resource = EM.GetComponent<ResourceComponent>(MineralID);
            auto* MineralPos = EM.GetComponent<TransformComponent>(MineralID);
            if (!Resource || !MineralPos)
                continue;
            const size_t Load = Resource->QueuedGatherers.size() +
                (Resource->ActiveGathererEntityID >= 0 ? 1u : 0u);
            const float Distance = WorkerPos
                ? WorkerPos->Position.DistanceTo(MineralPos->Position) : 0.0f;
            if (Load < BestLoad || (Load == BestLoad && Distance < BestDistance))
            {
                BestLoad = Load;
                BestDistance = Distance;
                BestMineral = MineralID;
            }
        }
        if (BestMineral >= 0)
            HarvestSystem::IssueHarvest(EM, {Eid}, BestMineral);
    }
}

void AIBrain::ManageMilitary(Core::ECS::EntityManager& EM)
{
    (void)EM;
}

bool AIBrain::DefendBaseIfNeeded(Core::ECS::EntityManager& EM)
{
    constexpr float kThreatRadius = 720.0f;
    constexpr float kDefenderRadius = 960.0f;
    int ThreatID = -1;
    Core::Math::Vec2 ThreatPosition;

    auto Buildings = EM.GetEntitiesWith<BuildingComponent, PlayerComponent,
                                        TransformComponent>();
    auto Units = EM.GetEntitiesWith<UnitComponent, PlayerComponent,
                                  TransformComponent>();
    for (int BuildingID : Buildings)
    {
        auto* BuildingPlayer = EM.GetComponent<PlayerComponent>(BuildingID);
        auto* BuildingPosition = EM.GetComponent<TransformComponent>(BuildingID);
        if (!BuildingPlayer || BuildingPlayer->PlayerID != mAIPlayerID ||
            !BuildingPosition)
        {
            continue;
        }

        for (int UnitID : Units)
        {
            auto* Unit = EM.GetComponent<UnitComponent>(UnitID);
            auto* UnitPlayer = EM.GetComponent<PlayerComponent>(UnitID);
            auto* UnitPosition = EM.GetComponent<TransformComponent>(UnitID);
            if (!Unit || !UnitPlayer || !UnitPosition || Unit->HitPoints <= 0 ||
                UnitPlayer->PlayerID == mAIPlayerID ||
                UnitPlayer->IsRescuePassive ||
                EM.GetComponent<ResourceComponent>(UnitID))
            {
                continue;
            }
            if (BuildingPosition->Position.DistanceTo(UnitPosition->Position) <=
                kThreatRadius)
            {
                ThreatID = UnitID;
                ThreatPosition = UnitPosition->Position;
                break;
            }
        }
        if (ThreatID >= 0)
            break;
    }

    if (ThreatID < 0)
    {
        if (mDefendingBase)
            Core::Utils::Logger::Info(
                "AIBrain", "Player " + std::to_string(mAIPlayerID) +
                " base defense cleared");
        return false;
    }

    std::vector<int> Defenders;
    for (int UnitID : Units)
    {
        auto* Unit = EM.GetComponent<UnitComponent>(UnitID);
        auto* Player = EM.GetComponent<PlayerComponent>(UnitID);
        auto* Position = EM.GetComponent<TransformComponent>(UnitID);
        auto* Combat = EM.GetComponent<CombatComponent>(UnitID);
        if (!Unit || !Player || !Position || !Combat ||
            Player->PlayerID != mAIPlayerID || IsWorkerUnit(*Unit) ||
            !IsCombatUnit(*Unit, *Combat) ||
            Position->Position.DistanceTo(mAiBasePos) > kDefenderRadius)
        {
            continue;
        }
        Defenders.push_back(UnitID);
    }

    if (!Defenders.empty())
    {
        if (!mDefendingBase)
        {
            mActiveAttackGroup.clear();
            mActiveAttackGroupAge = 0.0f;
        }
        CombatSystem::IssueAttackMove(EM, Defenders, ThreatPosition);
        if (!mDefendingBase)
        {
            Core::Utils::Logger::Info(
                "AIBrain", "Player " + std::to_string(mAIPlayerID) +
                " base defense engaged defenders=" +
                std::to_string(Defenders.size()) + " threat=" +
                std::to_string(ThreatID));
        }
    }
    return true;
}

void AIBrain::ManageTerranRepairs(Core::ECS::EntityManager& EM)
{
    struct RepairCandidate
    {
        int EntityID = -1;
        int Priority = 3;
        float HealthRatio = 1.0f;
        Core::Math::Vec2 Position;
        const char* PriorityName = "other_building";
    };

    int ActiveRepairers = 0;
    std::vector<int> AssignedTargets;
    for (int EntityID : EM.GetEntitiesWith<UnitComponent, PlayerComponent,
                                            CommandComponent>())
    {
        auto* Unit = EM.GetComponent<UnitComponent>(EntityID);
        auto* Player = EM.GetComponent<PlayerComponent>(EntityID);
        auto* Command = EM.GetComponent<CommandComponent>(EntityID);
        const auto* Order = Command ? Command->GetCurrentOrder() : nullptr;
        if (Unit && Player && Player->PlayerID == mAIPlayerID &&
            Unit->ChkUnitID == 7 && Order && Order->Type == OrderType::Repair)
        {
            ++ActiveRepairers;
            AssignedTargets.push_back(Order->TargetEntityID);
        }
    }
    constexpr int kMaxRepairers = 2;
    if (ActiveRepairers >= kMaxRepairers)
        return;

    RepairCandidate Best;
    bool Found = false;
    for (int EntityID : EM.GetEntitiesWith<UnitComponent, PlayerComponent,
                                            TransformComponent>())
    {
        auto* Unit = EM.GetComponent<UnitComponent>(EntityID);
        auto* Player = EM.GetComponent<PlayerComponent>(EntityID);
        auto* Transform = EM.GetComponent<TransformComponent>(EntityID);
        auto* Building = EM.GetComponent<BuildingComponent>(EntityID);
        if (!Unit || !Player || !Transform ||
            Player->PlayerID != mAIPlayerID || Unit->HitPoints <= 0 ||
            Unit->HitPoints >= Unit->MaxHitPoints ||
            Unit->UnitRace != Race::Terran ||
            EM.GetComponent<ResourceComponent>(EntityID) ||
            std::find(AssignedTargets.begin(), AssignedTargets.end(), EntityID) !=
                AssignedTargets.end())
        {
            continue;
        }

        RepairCandidate Candidate;
        Candidate.EntityID = EntityID;
        Candidate.Position = Transform->Position;
        Candidate.HealthRatio = Unit->MaxHitPoints > 0
            ? (float)Unit->HitPoints / (float)Unit->MaxHitPoints : 1.0f;
        const auto* Combat = EM.GetComponent<CombatComponent>(EntityID);
        if (!Building && IsTerranMechanicalUnit(*Unit) && Combat &&
            IsCombatUnit(*Unit, *Combat))
        {
            Candidate.Priority = 0;
            Candidate.PriorityName = "combat_mechanical";
        }
        else if (Building && !Building->UnderConstruction)
        {
            const auto Def = Game::Races::GetUnitDefByID(
                ResolveOwnedDefID(EM, EntityID));
            Candidate.Priority = IsTerranProductionBuilding(Def.Name) ? 1 : 2;
            Candidate.PriorityName = Candidate.Priority == 1
                ? "production_building" : "other_building";
        }
        else
        {
            continue;
        }

        if (!Found || Candidate.Priority < Best.Priority ||
            (Candidate.Priority == Best.Priority &&
             Candidate.HealthRatio < Best.HealthRatio))
        {
            Best = Candidate;
            Found = true;
        }
    }
    if (!Found)
        return;

    int BestScv = -1;
    float BestDistance = std::numeric_limits<float>::max();
    for (int EntityID : EM.GetEntitiesWith<UnitComponent, PlayerComponent,
                                            TransformComponent>())
    {
        auto* Unit = EM.GetComponent<UnitComponent>(EntityID);
        auto* Player = EM.GetComponent<PlayerComponent>(EntityID);
        auto* Transform = EM.GetComponent<TransformComponent>(EntityID);
        auto* Command = EM.GetComponent<CommandComponent>(EntityID);
        const auto* Order = Command ? Command->GetCurrentOrder() : nullptr;
        if (!Unit || !Player || !Transform || Unit->HitPoints <= 0 ||
            Unit->ChkUnitID != 7 || Player->PlayerID != mAIPlayerID ||
            IsAssignedToConstruction(EM, EntityID) ||
            (Order && Order->Type == OrderType::Repair))
        {
            continue;
        }
        const float Distance = Transform->Position.DistanceTo(Best.Position);
        if (Distance < BestDistance)
        {
            BestDistance = Distance;
            BestScv = EntityID;
        }
    }
    if (BestScv < 0)
        return;

    CommandSystem::InterruptHarvest(EM, BestScv);
    Order RepairOrder;
    RepairOrder.Type = OrderType::Repair;
    RepairOrder.TargetEntityID = Best.EntityID;
    RepairOrder.TargetPosition = Best.Position;
    RepairOrder.PathingIgnoreEntityID = Best.EntityID;
    CommandSystem::IssueOrder(EM, BestScv, RepairOrder, false);
    Core::Utils::Logger::Info(
        "AIBrain", "Player " + std::to_string(mAIPlayerID) +
        " repair assigned scv=" + std::to_string(BestScv) +
        " target=" + std::to_string(Best.EntityID) +
        " priority=" + Best.PriorityName);
}

void AIBrain::SendAttackWave(Core::ECS::EntityManager& EM, int WaveSize)
{
    if (mAttackCooldownRemaining > 0.0f)
        return;

    // Resource-depleted late games must not settle into two armies guarding
    // opposite corners forever.  After 45 simulated minutes, release any
    // surviving old group and commit every available combat unit to the next
    // living opponent.  The normal cooldown still limits order churn.
    const bool FinalAssault = mElapsedSeconds >= 2700.0f;

    mActiveAttackGroup.erase(
        std::remove_if(
            mActiveAttackGroup.begin(), mActiveAttackGroup.end(),
            [&](int EntityID)
            {
                auto* Unit = EM.GetComponent<UnitComponent>(EntityID);
                auto* Player = EM.GetComponent<PlayerComponent>(EntityID);
                auto* Combat = EM.GetComponent<CombatComponent>(EntityID);
                return !EM.IsAlive(EntityID) || !Unit || !Player || !Combat ||
                       Player->PlayerID != mAIPlayerID ||
                       !IsCombatUnit(*Unit, *Combat) || IsWorkerUnit(*Unit);
            }),
        mActiveAttackGroup.end());

    // SC1-style simple AI normally waits for the current fixed group to be
    // destroyed. A route-blocked or disengaged group is released after a
    // bounded interval so it cannot freeze every later attack wave.
    if (!FinalAssault && (int)mActiveAttackGroup.size() > 2 &&
        mActiveAttackGroupAge < kAttackGroupMaxAgeSeconds)
        return;
    mActiveAttackGroup.clear();
    mActiveAttackGroupAge = 0.0f;

    std::vector<int> Attackers;
    auto Units = EM.GetEntitiesWith<UnitComponent, PlayerComponent, CombatComponent, TransformComponent>();
    for (int Eid : Units)
    {
        auto* U = EM.GetComponent<UnitComponent>(Eid);
        auto* P = EM.GetComponent<PlayerComponent>(Eid);
        auto* C = EM.GetComponent<CombatComponent>(Eid);
        if (!U || !P || !C) continue;
        if (P->PlayerID != mAIPlayerID) continue;
        if (!IsCombatUnit(*U, *C))
            continue;

        const bool Worker = IsWorkerUnit(*U);
        if (Worker && !FinalAssault)
            continue;

        // In sudden death, idle workers join the assault, but a worker which
        // is already committed to a live mineral patch must finish that last
        // patch first. Otherwise the smallest mineral frame remains forever
        // while every worker has been reassigned to combat.
        if (Worker)
        {
            const auto* Harvest = EM.GetComponent<HarvestComponent>(Eid);
            const auto* Resource = Harvest
                ? EM.GetComponent<ResourceComponent>(Harvest->TargetResourceID)
                : nullptr;
            if (Harvest && Harvest->State != HarvestState::Idle && Resource &&
                Resource->Type == ResourceType::Minerals &&
                !Resource->IsDepleted && Resource->Amount > 0)
            {
                continue;
            }
        }

        Attackers.push_back(Eid);
    }

    if (!Attackers.empty() &&
        (FinalAssault || (int)Attackers.size() >= WaveSize))
    {
        if (!FinalAssault)
            Attackers.resize((size_t)WaveSize);
        mEnemyBasePos = FindEnemyBaseLocation(EM);
        int DirectTarget = -1;
        for (int EntityID : EM.GetEntitiesWith<PlayerComponent,
                                               BuildingComponent,
                                               UnitComponent>())
        {
            auto* Player = EM.GetComponent<PlayerComponent>(EntityID);
            auto* Unit = EM.GetComponent<UnitComponent>(EntityID);
            if (Player && Player->PlayerID == mTargetPlayerID && Unit &&
                Unit->HitPoints > 0 &&
                !EM.GetComponent<ResourceComponent>(EntityID))
            {
                DirectTarget = EntityID;
                break;
            }
        }
        if (DirectTarget < 0)
        {
            for (int EntityID : EM.GetEntitiesWith<PlayerComponent,
                                                   UnitComponent,
                                                   TransformComponent>())
            {
                auto* Player = EM.GetComponent<PlayerComponent>(EntityID);
                auto* Unit = EM.GetComponent<UnitComponent>(EntityID);
                if (Player && Player->PlayerID == mTargetPlayerID && Unit &&
                    Unit->HitPoints > 0 &&
                    !EM.GetComponent<ResourceComponent>(EntityID))
                {
                    DirectTarget = EntityID;
                    break;
                }
            }
        }
        if (DirectTarget >= 0)
            CombatSystem::IssueAttack(EM, Attackers, DirectTarget);
        else
            CombatSystem::IssueAttackMove(EM, Attackers, mEnemyBasePos);
        mActiveAttackGroup = Attackers;
        mActiveAttackGroupAge = 0.0f;
        mAttackWaveCount++;
        mAttackCooldownRemaining = kAttackWaveCooldownSeconds;
        Core::Utils::Logger::Info(
            "AIBrain", "Player " + std::to_string(mAIPlayerID) +
            " attack_wave=" + std::to_string(mAttackWaveCount) +
            " units=" + std::to_string(Attackers.size()) +
            " target_player=" + std::to_string(mTargetPlayerID) +
            " target=" + std::to_string((int)mEnemyBasePos.X) + "," +
            std::to_string((int)mEnemyBasePos.Y) +
            " direct_target=" + std::to_string(DirectTarget) +
            " final_assault=" + std::to_string(FinalAssault ? 1 : 0));
    }
}

void AIBrain::BuildOrTrain(Core::ECS::EntityManager& EM, int DefID, const Core::Math::Vec2& NearPos)
{
    const auto& Def = Game::Races::GetUnitDefByID(DefID);
    if (Def.IsBuilding)
    {
        int Builder = FindIdleWorker(EM);
        if (Builder >= 0)
            StartBuildingNear(EM, Builder, DefID, NearPos);
    }
    else
    {
        TrainUnitIfPossible(EM, DefID);
    }
}

void AIBrain::Update(float DeltaTime)
{
    if (DeltaTime <= 0.0f) return;
    auto& EM = mSystemMgr.GetEntityManager();
    mElapsedSeconds += DeltaTime;
    if (!mActiveAttackGroup.empty())
        mActiveAttackGroupAge += DeltaTime;

    if (!mShowcaseMode)
        RetargetIfNeeded(EM);

    mAttackCooldownRemaining =
        std::max(0.0f, mAttackCooldownRemaining - DeltaTime);

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

    mDefendingBase = DefendBaseIfNeeded(EM);

    int RaceVal = GetRace(EM);
    if (RaceVal == (int)Race::Terran)
        ManageTerranRepairs(EM);
    if (mShowcaseMode && !mShowcaseStartedLogged)
    {
        mShowcaseStartedLogged = true;
        Core::Utils::Logger::Info(
            "AIBrain", "Showcase decision loop active race=" +
            std::to_string(RaceVal) + " player=" + std::to_string(mAIPlayerID) +
            " base=" + std::to_string((int)mAiBasePos.X) + "," +
            std::to_string((int)mAiBasePos.Y));
    }
    if (mShowcaseMode && RaceVal == (int)Race::Terran)
    {
        RunTerranShowcase(EM);
        return;
    }
    if (mShowcaseMode && RaceVal == (int)Race::Zerg)
    {
        RunZergShowcase(EM);
        return;
    }
    if (mShowcaseMode && RaceVal == (int)Race::Protoss)
    {
        RunProtossShowcase(EM);
        return;
    }
    switch (RaceVal)
    {
        case 0: RunTerranAI(EM); break;
        case 1: RunZergAI(EM); break;
        case 2: RunProtossAI(EM); break;
        default: RunTerranAI(EM); break;
    }
}

} // namespace Game::Systems
