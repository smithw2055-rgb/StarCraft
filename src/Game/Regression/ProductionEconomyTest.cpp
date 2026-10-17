#include "Game/Systems/ProductionSystem.h"

#include "Game/Map/Map.h"
#include "Game/Races/RaceDef.h"
#include "Game/Systems/CombatSystem.h"
#include "Game/Systems/BuildingSystem.h"
#include "Game/Systems/CommandSystem.h"
#include "Game/Systems/DeathEvents.h"
#include "Game/Systems/HarvestSystem.h"
#include "Game/Systems/MovementSystem.h"
#include "Game/Systems/UpgradeSystem.h"
#include "Core/ECS/SystemManager.h"
#include "Game/Systems/ProtossSystem.h"
#include "Game/Systems/ZergSystem.h"

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>

namespace Game::Systems
{

void CombatSystem::IssueAttack(Core::ECS::EntityManager&,
                               const std::vector<int>&,
                               int)
{
}

} // namespace Game::Systems

namespace
{
    void Require(bool Value, const char* Message)
    {
        if (!Value)
        {
            std::fprintf(stderr, "FAIL: %s\n", Message);
            std::exit(1);
        }
    }

    int CreateCompletedBuilding(Core::ECS::EntityManager& EM,
                                const char* DefName,
                                int PlayerID,
                                Core::Math::Vec2 Position = {0.0f, 0.0f})
    {
        const int DefID = Game::Races::GetUnitDefID(DefName);
        const auto Def = Game::Races::GetUnitDefByID(DefID);
        int EntityID = EM.CreateEntity();
        EM.AddComponent<Game::Components::TransformComponent>(EntityID).Position = Position;
        auto& Unit = EM.AddComponent<Game::Components::UnitComponent>(EntityID);
        Unit.ChkUnitID = Def.ChkUnitID;
        Unit.UnitRace = Def.RaceType;
        Unit.HitPoints = Def.HitPoints;
        Unit.MaxHitPoints = Def.HitPoints;
        EM.AddComponent<Game::Components::PlayerComponent>(EntityID).PlayerID = PlayerID;
        auto& Building = EM.AddComponent<Game::Components::BuildingComponent>(EntityID);
        Building.IsBuilding = true;
        Building.UnderConstruction = false;
        Building.BuildingDefID = DefID;
        return EntityID;
    }

    int CreateCompletedBuildingWithoutTransform(Core::ECS::EntityManager& EM,
                                                const char* DefName,
                                                int PlayerID)
    {
        const int DefID = Game::Races::GetUnitDefID(DefName);
        const auto Def = Game::Races::GetUnitDefByID(DefID);
        int EntityID = EM.CreateEntity();
        auto& Unit = EM.AddComponent<Game::Components::UnitComponent>(EntityID);
        Unit.ChkUnitID = Def.ChkUnitID;
        Unit.UnitRace = Def.RaceType;
        Unit.HitPoints = Def.HitPoints;
        Unit.MaxHitPoints = Def.HitPoints;
        EM.AddComponent<Game::Components::PlayerComponent>(EntityID).PlayerID = PlayerID;
        auto& Building = EM.AddComponent<Game::Components::BuildingComponent>(EntityID);
        Building.IsBuilding = true;
        Building.UnderConstruction = false;
        Building.BuildingDefID = DefID;
        return EntityID;
    }

    int CreateCompletedUnit(Core::ECS::EntityManager& EM,
                            const char* DefName,
                            int PlayerID,
                            Core::Math::Vec2 Position = {0.0f, 0.0f})
    {
        const int DefID = Game::Races::GetUnitDefID(DefName);
        const auto Def = Game::Races::GetUnitDefByID(DefID);
        int EntityID = EM.CreateEntity();
        EM.AddComponent<Game::Components::TransformComponent>(EntityID).Position = Position;
        auto& Unit = EM.AddComponent<Game::Components::UnitComponent>(EntityID);
        Unit.ChkUnitID = Def.ChkUnitID;
        Unit.UnitRace = Def.RaceType;
        Unit.HitPoints = Def.HitPoints;
        Unit.MaxHitPoints = Def.HitPoints;
        Unit.SupplyCost = Def.SupplyCost;
        EM.AddComponent<Game::Components::PlayerComponent>(EntityID).PlayerID = PlayerID;
        return EntityID;
    }

    int CountUnitsByDef(Core::ECS::EntityManager& EM, const char* DefName, int PlayerID)
    {
        const int DefID = Game::Races::GetUnitDefID(DefName);
        const auto Def = Game::Races::GetUnitDefByID(DefID);
        int Count = 0;
        for (int EntityID : EM.GetEntitiesWith<Game::Components::UnitComponent,
                                               Game::Components::PlayerComponent>())
        {
            auto* Unit = EM.GetComponent<Game::Components::UnitComponent>(EntityID);
            auto* Player = EM.GetComponent<Game::Components::PlayerComponent>(EntityID);
            if (Unit && Player && Player->PlayerID == PlayerID &&
                Unit->ChkUnitID == Def.ChkUnitID)
            {
                ++Count;
            }
        }
        return Count;
    }

    int FindUnitByDef(Core::ECS::EntityManager& EM, const char* DefName, int PlayerID)
    {
        const int DefID = Game::Races::GetUnitDefID(DefName);
        const auto Def = Game::Races::GetUnitDefByID(DefID);
        for (int EntityID : EM.GetEntitiesWith<Game::Components::UnitComponent,
                                               Game::Components::PlayerComponent>())
        {
            auto* Unit = EM.GetComponent<Game::Components::UnitComponent>(EntityID);
            auto* Player = EM.GetComponent<Game::Components::PlayerComponent>(EntityID);
            if (Unit && Player && Player->PlayerID == PlayerID &&
                Unit->ChkUnitID == Def.ChkUnitID)
            {
                return EntityID;
            }
        }
        return -1;
    }

    int FindMorphByTarget(Core::ECS::EntityManager& EM, int TargetDefID, int PlayerID)
    {
        for (int EntityID : EM.GetEntitiesWith<Game::Components::MorphComponent,
                                               Game::Components::PlayerComponent>())
        {
            auto* Morph = EM.GetComponent<Game::Components::MorphComponent>(EntityID);
            auto* Player = EM.GetComponent<Game::Components::PlayerComponent>(EntityID);
            if (Morph && Player && Player->PlayerID == PlayerID &&
                Morph->MorphTargetDefID == TargetDefID && Morph->IsMorphing)
            {
                return EntityID;
            }
        }
        return -1;
    }

    int FindBuildingByDef(Core::ECS::EntityManager& EM, int TargetDefID, int PlayerID)
    {
        for (int EntityID : EM.GetEntitiesWith<Game::Components::BuildingComponent,
                                               Game::Components::PlayerComponent>())
        {
            auto* Building = EM.GetComponent<Game::Components::BuildingComponent>(EntityID);
            auto* Player = EM.GetComponent<Game::Components::PlayerComponent>(EntityID);
            if (Building && Player && Player->PlayerID == PlayerID &&
                Building->BuildingDefID == TargetDefID)
                return EntityID;
        }
        return -1;
    }
}

int main()
{
    Core::ECS::SystemManager SysMgr;
    auto& EM = SysMgr.GetEntityManager();
    Game::Systems::ZergSystem::SetImageResolver(
        [](int ChkUnitID) { return 50000u + (uint32_t)ChkUnitID; });
    Game::Systems::ProductionSystem::SetSubunitImageResolver(
        [](int ChkUnitID) { return 60000u + (uint32_t)ChkUnitID; });

    constexpr int PlayerID = 0;
    auto& Resources = Game::Systems::HarvestSystem::GetResources(PlayerID);
    Resources.Minerals = 100;
    Resources.VespeneGas = 0;
    Resources.SupplyUsed = 9;
    Resources.SupplyMax = 10;

    const int CommandCenter = CreateCompletedBuilding(EM, "CommandCenter", PlayerID);
    const int ScvDefID = Game::Races::GetUnitDefID("SCV");

    const int SpawnProbeBarracks = CreateCompletedBuilding(
        EM, "Barracks", 10, {1500.0f, 900.0f});
    auto& SpawnProbeBarracksPath =
        EM.AddComponent<Game::Components::PathfindingComponent>(SpawnProbeBarracks);
    SpawnProbeBarracksPath.HasCollisionBounds = true;
    SpawnProbeBarracksPath.CollisionLeft = 48;
    SpawnProbeBarracksPath.CollisionRight = 48;
    SpawnProbeBarracksPath.CollisionUp = 32;
    SpawnProbeBarracksPath.CollisionDown = 32;
    const int SpawnExitBlocker =
        CreateCompletedUnit(EM, "Marine", 10, {1500.0f, 954.0f});
    const auto MarineSpawn = Game::Systems::ProductionSystem::FindSpawnPosition(
        EM, SpawnProbeBarracks, Game::Races::GetUnitDefByID(
            Game::Races::GetUnitDefID("Marine")).ChkUnitID);
    const auto* SpawnExitBlockerTransform =
        EM.GetComponent<Game::Components::TransformComponent>(SpawnExitBlocker);
    Require(SpawnExitBlockerTransform && MarineSpawn.Y > 900.0f &&
            MarineSpawn.DistanceTo(SpawnExitBlockerTransform->Position) >= 32.0f,
        "production buildings choose an unoccupied exit below the building");

    const int CongestedBarracks = CreateCompletedBuilding(
        EM, "Barracks", 11, {2500.0f, 900.0f});
    auto& CongestedBarracksPath =
        EM.AddComponent<Game::Components::PathfindingComponent>(CongestedBarracks);
    CongestedBarracksPath.HasCollisionBounds = true;
    CongestedBarracksPath.CollisionLeft = 48;
    CongestedBarracksPath.CollisionRight = 48;
    CongestedBarracksPath.CollisionUp = 32;
    CongestedBarracksPath.CollisionDown = 32;
    std::vector<int> CongestedExitBlockers;
    for (int Row = 0; Row < 10; ++Row)
    {
        for (int Column = 0; Column <= 10; ++Column)
        {
            const int SignedColumn = Column == 0
                ? 0
                : ((Column & 1) ? -(Column + 1) / 2 : Column / 2);
            CongestedExitBlockers.push_back(CreateCompletedUnit(
                EM,
                "Marine",
                11,
                {2500.0f + (float)SignedColumn * 24.0f,
                 954.0f + (float)Row * 24.0f}));
        }
    }
    const auto CongestedSpawn = Game::Systems::ProductionSystem::FindSpawnPosition(
        EM, CongestedBarracks, Game::Races::GetUnitDefByID(
            Game::Races::GetUnitDefID("Marine")).ChkUnitID);
    bool CongestedSpawnOverlapsBlocker = false;
    for (const int BlockerID : CongestedExitBlockers)
    {
        const auto* BlockerTransform =
            EM.GetComponent<Game::Components::TransformComponent>(BlockerID);
        CongestedSpawnOverlapsBlocker = CongestedSpawnOverlapsBlocker ||
            (BlockerTransform &&
             std::fabs(CongestedSpawn.X - BlockerTransform->Position.X) < 32.0f &&
             std::fabs(CongestedSpawn.Y - BlockerTransform->Position.Y) < 32.0f);
    }
    const bool CongestedSpawnOverlapsProducer =
        CongestedSpawn.X > 2500.0f - 48.0f - 16.0f &&
        CongestedSpawn.X < 2500.0f + 49.0f + 16.0f &&
        CongestedSpawn.Y > 900.0f - 32.0f - 16.0f &&
        CongestedSpawn.Y < 900.0f + 33.0f + 16.0f;
    Require(CongestedSpawn.X >= 0.0f && CongestedSpawn.Y >= 0.0f &&
            !CongestedSpawnOverlapsBlocker && !CongestedSpawnOverlapsProducer,
        "saturated production lanes expand the exit search instead of spawning inside a unit");

    Require(Game::Systems::ProductionSystem::StartTrain(EM, CommandCenter, ScvDefID),
        "first SCV can be queued when one supply remains");
    Require(Resources.Minerals == 50, "training reserves mineral cost");
    Require(Resources.SupplyUsed == 10, "training reserves supply immediately");

    Require(!Game::Systems::ProductionSystem::StartTrain(EM, CommandCenter, ScvDefID),
        "second SCV is blocked by reserved supply");
    Require(Resources.Minerals == 50, "failed queue does not spend minerals");
    Require(Resources.SupplyUsed == 10, "failed queue does not change reserved supply");

    Require(Game::Systems::ProductionSystem::CancelTrain(EM, CommandCenter, 0),
        "cancel queued SCV succeeds");
    Require(Resources.Minerals == 100, "cancel refunds mineral cost");
    Require(Resources.SupplyUsed == 9, "cancel releases reserved supply");

    Resources.Minerals = 100;
    Resources.VespeneGas = 0;
    Resources.SupplyUsed = 8;
    Resources.SupplyMax = 10;

    Require(Game::Systems::ProductionSystem::StartTrain(EM, CommandCenter, ScvDefID),
        "first duplicate SCV can be queued for cancel regression");
    Require(Game::Systems::ProductionSystem::StartTrain(EM, CommandCenter, ScvDefID),
        "second duplicate SCV can be queued for cancel regression");

    Game::Systems::ProductionSystem Production(SysMgr);
    const auto ScvDef = Game::Races::GetUnitDefByID(ScvDefID);
    Production.Update((float)ScvDef.BuildTime * 0.5f);

    constexpr int EnergyCapSpawnPlayerID = 11;
    Game::Systems::UpgradeSystem::GetUpgrades(EnergyCapSpawnPlayerID)
        .Levels[static_cast<size_t>(Game::Systems::ResearchId::Terran_ApolloReactor)] = 1;
    const int UpgradedWraith = Game::Systems::ProductionSystem::DebugSpawnUnit(
        EM,
        Game::Races::GetUnitDefID("Wraith"),
        {128.0f, 128.0f},
        EnergyCapSpawnPlayerID);
    auto* UpgradedWraithUnit =
        EM.GetComponent<Game::Components::UnitComponent>(UpgradedWraith);
    Require(UpgradedWraithUnit && UpgradedWraithUnit->MaxEnergyPoints == 250,
        "newly spawned energy units apply completed reactor max energy upgrades");
    const int BaselineCarrier = Game::Systems::ProductionSystem::DebugSpawnUnit(
        EM,
        Game::Races::GetUnitDefID("Carrier"),
        {160.0f, 128.0f},
        EnergyCapSpawnPlayerID);
    Require(Game::Systems::UpgradeSystem::GetMaxHangarCount(EM, BaselineCarrier) == 4,
        "Carrier starts with four interceptor capacity before capacity upgrade");
    Game::Systems::UpgradeSystem::GetUpgrades(EnergyCapSpawnPlayerID)
        .Levels[static_cast<size_t>(Game::Systems::ResearchId::Protoss_CarrierCapacity)] = 1;
    Require(Game::Systems::UpgradeSystem::GetMaxHangarCount(EM, BaselineCarrier) == 8,
        "Carrier Capacity raises interceptor capacity to eight");
    Game::Systems::UpgradeSystem::GetUpgrades(EnergyCapSpawnPlayerID)
        .Levels[static_cast<size_t>(Game::Systems::ResearchId::Protoss_ReaverCapacity)] = 1;
    const int UpgradedReaver = Game::Systems::ProductionSystem::DebugSpawnUnit(
        EM,
        Game::Races::GetUnitDefID("Reaver"),
        {192.0f, 128.0f},
        EnergyCapSpawnPlayerID);
    Require(Game::Systems::UpgradeSystem::GetMaxHangarCount(EM, UpgradedReaver) == 10,
        "Reaver Capacity raises scarab capacity to ten");

    auto* Prod = EM.GetComponent<Game::Components::ProductionComponent>(CommandCenter);
    Require(Prod != nullptr, "command center has production component");
    Require(Prod->Queue.size() == 2, "duplicate SCV queue has two entries");
    Require(Prod->CurrentTrainingUnitDefID == ScvDefID,
        "first duplicate SCV is current training item");
    const float ProgressBeforeCancel = Prod->TrainingProgress;
    Require(ProgressBeforeCancel > 0.0f, "first duplicate SCV has training progress");

    Require(Game::Systems::ProductionSystem::CancelTrain(EM, CommandCenter, 1),
        "canceling second duplicate SCV succeeds");
    Require(Prod->Queue.size() == 1, "canceling second duplicate SCV removes only queued item");
    Require(Prod->CurrentTrainingUnitDefID == ScvDefID,
        "canceling second duplicate SCV keeps first SCV training");
    Require(Prod->TrainingProgress == ProgressBeforeCancel,
        "canceling second duplicate SCV preserves current training progress");
    Require(Resources.Minerals == 50, "canceling second duplicate SCV refunds one mineral cost");
    Require(Resources.SupplyUsed == 9, "canceling second duplicate SCV releases one reserved supply");

    Resources.Minerals = 100;
    Resources.VespeneGas = 0;
    Resources.SupplyUsed = 0;
    Resources.SupplyMax = 10;
    auto* CommandCenterBuilding =
        EM.GetComponent<Game::Components::BuildingComponent>(CommandCenter);
    Require(CommandCenterBuilding != nullptr,
        "lifted production coverage has command center building data");
    CommandCenterBuilding->IsLifted = true;
    Require(!Game::Systems::ProductionSystem::StartTrain(EM, CommandCenter, ScvDefID),
        "lifted production building cannot start training");
    Require(Resources.Minerals == 100 && Resources.SupplyUsed == 0,
        "lifted production building does not reserve resources or supply");
    CommandCenterBuilding->IsLifted = false;

    constexpr int DeadProductionPlayerID = 4;
    auto& DeadProductionResources = Game::Systems::HarvestSystem::GetResources(DeadProductionPlayerID);
    DeadProductionResources.Minerals = 50;
    DeadProductionResources.VespeneGas = 0;
    DeadProductionResources.SupplyUsed = 0;
    DeadProductionResources.SupplyMax = 10;

    const int DeadBarracks =
        CreateCompletedBuilding(EM, "Barracks", DeadProductionPlayerID, {160.0f, 160.0f});
    const int MarineDefID = Game::Races::GetUnitDefID("Marine");
    auto* DeadBarracksUnit = EM.GetComponent<Game::Components::UnitComponent>(DeadBarracks);
    Require(DeadBarracksUnit != nullptr, "dead producer coverage has barracks unit data");
    DeadBarracksUnit->HitPoints = 0;
    Require(!Game::Systems::ProductionSystem::StartTrain(EM, DeadBarracks, MarineDefID),
        "dead production building cannot start training");
    Require(DeadProductionResources.Minerals == 50 &&
            DeadProductionResources.SupplyUsed == 0,
        "dead producer start attempt does not reserve resources or supply");

    DeadProductionResources.Minerals = 100;
    DeadProductionResources.SupplyUsed = 0;
    const int DoomedBarracks =
        CreateCompletedBuilding(EM, "Barracks", DeadProductionPlayerID, {192.0f, 160.0f});
    Require(Game::Systems::ProductionSystem::StartTrain(EM, DoomedBarracks, MarineDefID),
        "live barracks queues first Marine before death");
    Require(Game::Systems::ProductionSystem::StartTrain(EM, DoomedBarracks, MarineDefID),
        "live barracks queues second Marine before death");
    auto* DoomedProd = EM.GetComponent<Game::Components::ProductionComponent>(DoomedBarracks);
    Require(DoomedProd && DoomedProd->Queue.size() == 2,
        "doomed barracks has queued Marines before death");
    auto* DoomedUnit = EM.GetComponent<Game::Components::UnitComponent>(DoomedBarracks);
    Require(DoomedUnit != nullptr, "doomed barracks has unit data before death");
    auto* DoomedRender =
        EM.GetComponent<Game::Components::RenderComponent>(DoomedBarracks);
    if (!DoomedRender)
    {
        DoomedRender = &EM.AddComponent<Game::Components::RenderComponent>(
            DoomedBarracks);
        DoomedRender->ImageID = 266u;
    }
    auto& DoomedTree =
        EM.AddComponent<Game::Components::ImageTreeComponent>(DoomedBarracks);
    auto* DoomedBuilding =
        EM.GetComponent<Game::Components::BuildingComponent>(DoomedBarracks);
    Require(DoomedBuilding != nullptr, "doomed barracks has building flight state");
    DoomedBuilding->IsLifted = true;
    DoomedBuilding->FlightPhase =
        Game::Components::BuildingFlightPhase::Airborne;
    DoomedUnit->HitPoints = 0;
    const int MarinesBeforeDoomedDeath =
        CountUnitsByDef(EM, "Marine", DeadProductionPlayerID);
    Game::Systems::RecordUnitDeath(EM, DoomedBarracks, -1, false);
    int BarracksDeathVisual = -1;
    for (int EffectID : EM.GetEntitiesWith<
             Game::Components::EffectComponent,
             Game::Components::RenderComponent,
             Game::Components::UnitComponent>())
    {
        const auto* EffectRender =
            EM.GetComponent<Game::Components::RenderComponent>(EffectID);
        const auto* Effect =
            EM.GetComponent<Game::Components::EffectComponent>(EffectID);
        if (EffectID != DoomedBarracks && EffectRender && Effect &&
            EffectRender->ImageID == DoomedRender->ImageID &&
            Effect->InitialAnimation == Game::IScriptAnims::Anim_Death)
        {
            BarracksDeathVisual = EffectID;
            break;
        }
    }
    const auto* BarracksDeathTree = BarracksDeathVisual >= 0
        ? EM.GetComponent<Game::Components::ImageTreeComponent>(BarracksDeathVisual)
        : nullptr;
    const auto* BarracksDeathBuilding = BarracksDeathVisual >= 0
        ? EM.GetComponent<Game::Components::BuildingComponent>(BarracksDeathVisual)
        : nullptr;
    const auto* BarracksDeathEffect = BarracksDeathVisual >= 0
        ? EM.GetComponent<Game::Components::EffectComponent>(BarracksDeathVisual)
        : nullptr;
    Require(BarracksDeathVisual >= 0 && BarracksDeathTree &&
            BarracksDeathTree->Nodes.size() == DoomedTree.Nodes.size() &&
            BarracksDeathBuilding && BarracksDeathBuilding->IsLifted &&
            BarracksDeathEffect &&
            BarracksDeathEffect->DestroyWhenAnimationCompletes &&
            BarracksDeathEffect->RenderBand == 2,
        "building death keeps its real root and child images for the authored Death IScript");
    Require(DoomedProd->Queue.empty() &&
            DoomedProd->CurrentTrainingUnitDefID == -1,
        "producer death clears its pending training queue");
    Require(DeadProductionResources.Minerals == 0,
        "producer death does not refund minerals from lost queued units");
    Require(DeadProductionResources.SupplyUsed == 0,
        "producer death releases reserved queued-unit supply");
    Production.Update((float)Game::Races::GetUnitDefByID(MarineDefID).BuildTime + 0.1f);
    Require(CountUnitsByDef(EM, "Marine", DeadProductionPlayerID) == MarinesBeforeDoomedDeath,
        "dead producer does not complete queued units after death");

    constexpr int DeadResearchPlayerID = 5;
    auto& DeadResearchResources =
        Game::Systems::HarvestSystem::GetResources(DeadResearchPlayerID);
    DeadResearchResources.Minerals = 300;
    DeadResearchResources.VespeneGas = 300;
    DeadResearchResources.SupplyUsed = 0;
    DeadResearchResources.SupplyMax = 10;
    const int DeadEngineeringBay =
        CreateCompletedBuilding(EM, "EngineeringBay", DeadResearchPlayerID, {224.0f, 160.0f});
    auto* DeadEngineeringBayUnit =
        EM.GetComponent<Game::Components::UnitComponent>(DeadEngineeringBay);
    Require(DeadEngineeringBayUnit != nullptr,
        "dead research producer coverage has engineering bay unit data");
    DeadEngineeringBayUnit->HitPoints = 0;
    Require(!Game::Systems::UpgradeSystem::StartResearch(
                EM, DeadEngineeringBay,
                Game::Systems::ResearchId::Terran_InfantryWeapons),
            "dead research building cannot start research");
    Require(DeadResearchResources.Minerals == 300 &&
            DeadResearchResources.VespeneGas == 300,
        "dead research start attempt does not spend resources");

    constexpr int OverlapResearchPlayerID = 6;
    auto& OverlapResearchResources =
        Game::Systems::HarvestSystem::GetResources(OverlapResearchPlayerID);
    OverlapResearchResources.Minerals = 300;
    OverlapResearchResources.VespeneGas = 300;
    OverlapResearchResources.SupplyUsed = 0;
    OverlapResearchResources.SupplyMax = 10;
    const int BusyEngineeringBay =
        CreateCompletedBuilding(EM, "EngineeringBay", OverlapResearchPlayerID, {240.0f, 192.0f});
    Require(Game::Systems::UpgradeSystem::StartResearch(
                EM, BusyEngineeringBay,
                Game::Systems::ResearchId::Terran_InfantryWeapons),
            "research building starts first upgrade");
    auto* BusyResearchBuilding =
        EM.GetComponent<Game::Components::BuildingComponent>(BusyEngineeringBay);
    Require(BusyResearchBuilding && BusyResearchBuilding->IsResearching &&
            BusyResearchBuilding->ResearchingID ==
                static_cast<int>(Game::Systems::ResearchId::Terran_InfantryWeapons),
        "research building tracks first active upgrade");
    const int DuplicateEngineeringBay =
        CreateCompletedBuilding(EM, "EngineeringBay", OverlapResearchPlayerID,
                                {272.0f, 192.0f});
    Require(!Game::Systems::UpgradeSystem::StartResearch(
                EM, DuplicateEngineeringBay,
                Game::Systems::ResearchId::Terran_InfantryWeapons),
        "a player cannot research the same upgrade in two buildings at once");
    Require(!Game::Systems::UpgradeSystem::StartResearch(
                EM, BusyEngineeringBay,
                Game::Systems::ResearchId::Terran_InfantryArmor),
            "research building cannot start another upgrade while busy");
    Require(OverlapResearchResources.Minerals == 200 &&
            OverlapResearchResources.VespeneGas == 200,
        "blocked overlapping research attempt does not spend resources");
    Require(BusyResearchBuilding->IsResearching &&
            BusyResearchBuilding->ResearchingID ==
                static_cast<int>(Game::Systems::ResearchId::Terran_InfantryWeapons),
        "blocked overlapping research attempt preserves first upgrade state");
    Require(Game::Systems::UpgradeSystem::CancelResearch(EM, BusyEngineeringBay),
        "manual research cancel succeeds");
    Require(!BusyResearchBuilding->IsResearching &&
            BusyResearchBuilding->ResearchingID == -1,
        "manual research cancel clears active research state");
    Require(OverlapResearchResources.Minerals == 300 &&
            OverlapResearchResources.VespeneGas == 300,
        "manual research cancel refunds reserved research resources");
    Require(Game::Systems::UpgradeSystem::GetResearchLevel(
                OverlapResearchPlayerID,
                Game::Systems::ResearchId::Terran_InfantryWeapons) == 0,
        "manual research cancel does not grant the canceled upgrade");
    Require(Game::Systems::UpgradeSystem::CanStartResearch(
                EM, DuplicateEngineeringBay,
                Game::Systems::ResearchId::Terran_InfantryWeapons),
        "manual research cancel releases the player-wide research lock");

    const int DoomedEngineeringBay =
        CreateCompletedBuilding(EM, "EngineeringBay", DeadResearchPlayerID, {256.0f, 160.0f});
    Require(Game::Systems::UpgradeSystem::StartResearch(
                EM, DoomedEngineeringBay,
                Game::Systems::ResearchId::Terran_InfantryWeapons),
            "live research building starts upgrade before death");
    auto* DoomedResearchBuilding =
        EM.GetComponent<Game::Components::BuildingComponent>(DoomedEngineeringBay);
    auto* DoomedResearchUnit =
        EM.GetComponent<Game::Components::UnitComponent>(DoomedEngineeringBay);
    Require(DoomedResearchBuilding && DoomedResearchBuilding->IsResearching &&
            DoomedResearchUnit,
        "doomed research building has active research state before death");
    DoomedResearchUnit->HitPoints = 0;
    Game::Systems::RecordUnitDeath(EM, DoomedEngineeringBay, -1, false);
    Require(!DoomedResearchBuilding->IsResearching &&
            DoomedResearchBuilding->ResearchingID == -1,
        "research building death clears active research state");
    Game::Systems::UpgradeSystem Upgrade(SysMgr);
    Upgrade.Update(1000.0f);
    Require(Game::Systems::UpgradeSystem::GetResearchLevel(
                DeadResearchPlayerID,
                Game::Systems::ResearchId::Terran_InfantryWeapons) == 0,
            "dead research building does not complete its upgrade after death");
    Require(DeadResearchResources.Minerals == 275 &&
            DeadResearchResources.VespeneGas == 275,
        "destroyed research building refunds three quarters of spent resources like OpenBW");
    const int ReplacementEngineeringBay =
        CreateCompletedBuilding(EM, "EngineeringBay", DeadResearchPlayerID,
                                {272.0f, 160.0f});
    Require(Game::Systems::UpgradeSystem::CanStartResearch(
                EM, ReplacementEngineeringBay,
                Game::Systems::ResearchId::Terran_InfantryWeapons),
        "research building death releases the player-wide research lock");

    constexpr int HighSlotResearchPlayerID = 8;
    auto& HighSlotResearchResources =
        Game::Systems::HarvestSystem::GetResources(HighSlotResearchPlayerID);
    HighSlotResearchResources.Minerals = 300;
    HighSlotResearchResources.VespeneGas = 300;
    HighSlotResearchResources.SupplyUsed = 0;
    HighSlotResearchResources.SupplyMax = 10;
    Require(Game::Systems::UpgradeSystem::GetResearchLevel(
                7,
                Game::Systems::ResearchId::Terran_InfantryArmor) == 0,
        "high-slot research coverage starts with player 7 infantry armor unresearched");
    Require(Game::Systems::UpgradeSystem::GetResearchLevel(
                HighSlotResearchPlayerID,
                Game::Systems::ResearchId::Terran_InfantryArmor) == 0,
        "high-slot research coverage starts with player 8 infantry armor unresearched");
    const int HighSlotEngineeringBay =
        CreateCompletedBuilding(EM, "EngineeringBay", HighSlotResearchPlayerID, {288.0f, 160.0f});
    Require(Game::Systems::UpgradeSystem::StartResearch(
                EM,
                HighSlotEngineeringBay,
                Game::Systems::ResearchId::Terran_InfantryArmor),
        "player 8 can start research in its own upgrade slot");
    Upgrade.Update(1000.0f);
    Require(Game::Systems::UpgradeSystem::GetResearchLevel(
                HighSlotResearchPlayerID,
                Game::Systems::ResearchId::Terran_InfantryArmor) == 1,
        "player 8 completed research is stored in player 8 upgrade slot");
    Require(Game::Systems::UpgradeSystem::GetResearchLevel(
                7,
                Game::Systems::ResearchId::Terran_InfantryArmor) == 0,
        "player 8 completed research does not alias player 7 upgrade slot");

    constexpr int BrokenStartPlayerID = 4;
    auto& BrokenStartResources = Game::Systems::HarvestSystem::GetResources(BrokenStartPlayerID);
    BrokenStartResources.Minerals = 100;
    BrokenStartResources.VespeneGas = 0;
    BrokenStartResources.SupplyUsed = 0;
    BrokenStartResources.SupplyMax = 10;

    const int BrokenStartCommandCenter =
        CreateCompletedBuildingWithoutTransform(EM, "CommandCenter", BrokenStartPlayerID);
    Require(!Game::Systems::ProductionSystem::CanTrain(EM, BrokenStartCommandCenter, ScvDefID),
        "producer without transform cannot train units");
    Require(!Game::Systems::ProductionSystem::StartTrain(EM, BrokenStartCommandCenter, ScvDefID),
        "producer without transform cannot spend resources by starting training");
    auto* BrokenStartProd =
        EM.GetComponent<Game::Components::ProductionComponent>(BrokenStartCommandCenter);
    Require(!BrokenStartProd || BrokenStartProd->Queue.empty(),
        "failed missing-transform training leaves no production queue");
    Require(BrokenStartResources.Minerals == 100,
        "missing-transform producer does not spend minerals");
    Require(BrokenStartResources.SupplyUsed == 0,
        "missing-transform producer does not reserve supply");

    constexpr int BrokenFinishPlayerID = 5;
    auto& BrokenFinishResources = Game::Systems::HarvestSystem::GetResources(BrokenFinishPlayerID);
    BrokenFinishResources.Minerals = 100 - ScvDef.MineralCost;
    BrokenFinishResources.VespeneGas = 0;
    BrokenFinishResources.SupplyUsed = ScvDef.SupplyCost;
    BrokenFinishResources.SupplyMax = 10;

    const int BrokenFinishCommandCenter =
        CreateCompletedBuildingWithoutTransform(EM, "CommandCenter", BrokenFinishPlayerID);
    auto& BrokenFinishProd =
        EM.AddComponent<Game::Components::ProductionComponent>(BrokenFinishCommandCenter);
    BrokenFinishProd.Queue.push_back(ScvDefID);
    BrokenFinishProd.CurrentTrainingUnitDefID = ScvDefID;
    BrokenFinishProd.TrainingProgress = (float)ScvDef.BuildTime;
    BrokenFinishProd.TrainingTimePerUnit = (float)ScvDef.BuildTime;

    Production.Update(0.1f);
    Require(BrokenFinishProd.Queue.empty(),
        "failed spawn completion removes the broken production queue item");
    Require(BrokenFinishProd.CurrentTrainingUnitDefID == -1,
        "failed spawn completion clears current training unit");
    Require(BrokenFinishResources.Minerals == 100,
        "failed spawn completion refunds reserved minerals");
    Require(BrokenFinishResources.SupplyUsed == 0,
        "failed spawn completion releases reserved supply");

    Resources.Minerals = 200;
    Resources.VespeneGas = 100;
    Resources.SupplyUsed = 0;
    Resources.SupplyMax = 20;

    const int Factory = CreateCompletedBuilding(EM, "Factory", PlayerID, {400.0f, 400.0f});
    const int MachineShop = CreateCompletedBuilding(EM, "MachineShop", PlayerID, {448.0f, 400.0f});
    const int Armory = CreateCompletedBuilding(EM, "Armory", PlayerID, {400.0f, 464.0f});
    (void)Armory;
    EM.GetComponent<Game::Components::BuildingComponent>(Factory)->AddonID = MachineShop;
    const Core::Math::Vec2 RallyPoint = {520.0f, 432.0f};
    Game::Systems::CommandSystem::SetMap(nullptr);
    Game::Systems::CommandSystem::SetRallyPoint(EM, Factory, RallyPoint);

    const int GoliathDefID = Game::Races::GetUnitDefID("Goliath");
    const auto GoliathDef = Game::Races::GetUnitDefByID(GoliathDefID);

    Require(Game::Systems::ProductionSystem::StartTrain(EM, Factory, GoliathDefID),
        "Goliath can be queued from a completed factory with armory");
    Production.Update((float)GoliathDef.BuildTime);

    int SpawnedGoliath = -1;
    for (int EntityID : EM.GetEntitiesWith<Game::Components::UnitComponent,
                                           Game::Components::CombatComponent>())
    {
        auto* Unit = EM.GetComponent<Game::Components::UnitComponent>(EntityID);
        if (Unit && Unit->ChkUnitID == GoliathDef.ChkUnitID)
        {
            SpawnedGoliath = EntityID;
            break;
        }
    }

    Require(SpawnedGoliath >= 0, "completed production spawns the trained Goliath");
    auto* GoliathRender = EM.GetComponent<Game::Components::RenderComponent>(SpawnedGoliath);
    Require(GoliathRender &&
            GoliathRender->SubunitImageID == 60000u + (uint32_t)GoliathDef.ChkUnitID,
        "produced Goliath keeps its units.dat head/turret subunit image");
    auto* GoliathCombat = EM.GetComponent<Game::Components::CombatComponent>(SpawnedGoliath);
    Require(GoliathCombat && GoliathCombat->CanTargetGround,
        "produced combat units keep ground targeting capability");
    Require(GoliathCombat->CanTargetAir,
        "produced combat units keep air targeting capability");
    Require(GoliathCombat->GroundDamage == GoliathDef.GroundDamage &&
            GoliathCombat->AirDamage == GoliathDef.AirDamage,
        "produced combat units use their definition weapon damage");
    Require(GoliathCombat->IsRanged &&
            GoliathCombat->WeaponBeh != Game::Components::WeaponBehavior::Melee,
        "produced ranged combat units keep projectile weapon metadata");
    auto* GoliathCommand = EM.GetComponent<Game::Components::CommandComponent>(SpawnedGoliath);
    auto* GoliathPath = EM.GetComponent<Game::Components::PathfindingComponent>(SpawnedGoliath);
    Require(GoliathCommand &&
            GoliathCommand->CurrentCommand == Game::Components::CommandType::Move,
        "produced units inherit the producer rally point as a move command");
    Require(GoliathCommand->TargetPosition.X == RallyPoint.X &&
            GoliathCommand->TargetPosition.Y == RallyPoint.Y,
        "produced unit move command targets the rally point");
    Require(GoliathPath && !GoliathPath->Path.empty() &&
            GoliathPath->Path.back().X == RallyPoint.X &&
            GoliathPath->Path.back().Y == RallyPoint.Y,
        "produced unit receives a path toward the rally point");

    Resources.Minerals = 300;
    Resources.VespeneGas = 100;
    Resources.SupplyUsed = 0;
    Resources.SupplyMax = 20;

    const int Gateway = CreateCompletedBuilding(EM, "Gateway", PlayerID, {640.0f, 400.0f});
    const int DragoonDefID = Game::Races::GetUnitDefID("Dragoon");
    const auto DragoonDef = Game::Races::GetUnitDefByID(DragoonDefID);
    Require(!Game::Systems::ProductionSystem::CanTrain(EM, Gateway, DragoonDefID),
        "Protoss Dragoon is blocked until Cybernetics Core prerequisite exists");
    const int CyberneticsCore = CreateCompletedBuilding(EM, "Cybernetics Core", PlayerID, {704.0f, 400.0f});
    Require(CyberneticsCore > 0, "completed Cybernetics Core can be spawned for prerequisite test");
    Require(Game::Systems::ProductionSystem::StartTrain(EM, Gateway, DragoonDefID),
        "Protoss Dragoon can be queued once Gateway producer and Cybernetics Core prerequisite are normalized");
    auto* GatewayProd = EM.GetComponent<Game::Components::ProductionComponent>(Gateway);
    Require(GatewayProd && !GatewayProd->Queue.empty() && GatewayProd->Queue.back() == DragoonDefID,
        "Gateway production queue records the Dragoon def id");
    Require(Resources.Minerals == 300 - DragoonDef.MineralCost &&
            Resources.VespeneGas == 100 - DragoonDef.GasCost,
        "queued Dragoon spends normalized Protoss unit costs");

    Resources.Minerals = 200;
    Resources.VespeneGas = 0;
    Resources.SupplyUsed = 0;
    Resources.SupplyMax = 20;

    const int Hatchery = CreateCompletedBuilding(EM, "Hatchery", PlayerID, {640.0f, 520.0f});
    const int ZerglingDefID = Game::Races::GetUnitDefID("Zergling");
    const auto ZerglingDef = Game::Races::GetUnitDefByID(ZerglingDefID);
    Require(!Game::Systems::ProductionSystem::CanTrain(EM, Hatchery, ZerglingDefID),
        "Zergling is blocked until Spawning Pool prerequisite exists");
    const int SpawningPool = CreateCompletedBuilding(EM, "Spawning Pool", PlayerID, {704.0f, 520.0f});
    Require(SpawningPool > 0, "completed Spawning Pool can be spawned for prerequisite test");
    Require(!Game::Systems::ProductionSystem::StartTrain(EM, Hatchery, ZerglingDefID),
        "Zergling cannot enter a Hatchery queue because it must morph from Larva");
    auto* HatcheryProd = EM.GetComponent<Game::Components::ProductionComponent>(Hatchery);
    Require(!HatcheryProd || HatcheryProd->Queue.empty(),
        "Hatchery production queue does not record the Zergling def id");
    Require(Resources.Minerals == 200,
        "rejected Hatchery queue does not spend resources");

    constexpr int ZergMorphPlayerID = 2;
    auto& ZergMorphResources = Game::Systems::HarvestSystem::GetResources(ZergMorphPlayerID);
    ZergMorphResources.Minerals = 300;
    ZergMorphResources.VespeneGas = 0;
    ZergMorphResources.SupplyUsed = 0;
    ZergMorphResources.SupplyMax = 20;

    const int LarvaWithoutPool = CreateCompletedUnit(EM, "Larva", ZergMorphPlayerID, {760.0f, 520.0f});
    Require(!Game::Systems::ZergSystem::CanMorphLarvaIntoUnit(EM, LarvaWithoutPool, ZerglingDefID),
        "Zerg larva morph is blocked until local Spawning Pool prerequisite is normalized");

    const int MorphSpawningPool = CreateCompletedBuilding(EM, "Spawning Pool", ZergMorphPlayerID, {800.0f, 520.0f});
    Require(MorphSpawningPool > 0, "completed Spawning Pool can be spawned for Zerg morph prerequisite test");
    const int LarvaWithPool = CreateCompletedUnit(EM, "Larva", ZergMorphPlayerID, {832.0f, 520.0f});
    Require(Game::Systems::ZergSystem::CanMorphLarvaIntoUnit(EM, LarvaWithPool, ZerglingDefID),
        "Zerg larva can morph Zergling once Spawning Pool prerequisite is normalized");
    Require(Game::Systems::ZergSystem::StartMorphLarvaIntoUnit(EM, LarvaWithPool, ZerglingDefID),
        "Zerg larva morph starts after normalized prerequisite check succeeds");
    Require(!EM.IsAlive(LarvaWithPool), "Zerg larva morph consumes the source larva");
    const int ZerglingEgg = FindMorphByTarget(EM, ZerglingDefID, ZergMorphPlayerID);
    auto* ZerglingEggRender = EM.GetComponent<Game::Components::RenderComponent>(ZerglingEgg);
    auto* ZerglingEggUnit =
        EM.GetComponent<Game::Components::UnitComponent>(ZerglingEgg);
    Require(ZerglingEgg > 0 && ZerglingEggRender && ZerglingEggUnit &&
            ZerglingEggRender->ImageID == 50000u + 0x24u,
        "Zerg larva morph creates a visible Egg using the Egg image id");
    Require(ZerglingEggUnit->ChkUnitID == 36 &&
            ZerglingEggUnit->HitPoints == 200 &&
            ZerglingEggUnit->MaxHitPoints == 200 &&
            ZerglingEggUnit->Armor == 10,
        "Zerg Egg uses its independent classic 200 HP and 10 armor definition");
    Require(ZergMorphResources.Minerals == 300 - ZerglingDef.MineralCost &&
            ZergMorphResources.SupplyUsed == ZerglingDef.SupplyCost,
        "Zerg larva morph reserves resources and supply");

    constexpr int ZergBuildingMorphPlayerID = 3;
    auto& ZergBuildingResources = Game::Systems::HarvestSystem::GetResources(ZergBuildingMorphPlayerID);
    ZergBuildingResources.Minerals = 500;
    ZergBuildingResources.VespeneGas = 300;
    ZergBuildingResources.SupplyUsed = 0;
    ZergBuildingResources.SupplyMax = 20;

    const int MorphHatchery = CreateCompletedBuilding(EM, "Hatchery", ZergBuildingMorphPlayerID, {880.0f, 520.0f});
    auto* MorphHatcheryUnit =
        EM.GetComponent<Game::Components::UnitComponent>(MorphHatchery);
    constexpr int MorphingBuildingMissingHitPoints = 123;
    Require(MorphHatcheryUnit &&
            MorphHatcheryUnit->MaxHitPoints > MorphingBuildingMissingHitPoints,
        "damaged building morph fixture has enough Hatchery vitality");
    MorphHatcheryUnit->HitPoints =
        MorphHatcheryUnit->MaxHitPoints - MorphingBuildingMissingHitPoints;
    const int LairDefID = Game::Races::GetUnitDefID("Lair");
    const auto LairDef = Game::Races::GetUnitDefByID(LairDefID);
    Require(!Game::Systems::ZergSystem::CanMorphBuilding(EM, MorphHatchery, LairDefID),
        "Zerg Hatchery to Lair morph is blocked until Spawning Pool prerequisite is normalized");
    const int BuildingMorphPool = CreateCompletedBuilding(EM, "Spawning Pool", ZergBuildingMorphPlayerID, {920.0f, 520.0f});
    Require(!Game::Systems::ZergSystem::CanMorphBuilding(EM, BuildingMorphPool, LairDefID),
        "Zerg building morph rejects an invalid source building even when prerequisites exist");
    Require(Game::Systems::ZergSystem::StartMorphBuilding(EM, MorphHatchery, LairDefID),
        "Zerg Hatchery starts Lair morph after normalized prerequisites exist");
    auto* HatcheryMorph = EM.GetComponent<Game::Components::MorphComponent>(MorphHatchery);
    auto* HatcheryBuilding = EM.GetComponent<Game::Components::BuildingComponent>(MorphHatchery);
    Require(HatcheryMorph && HatcheryMorph->MorphTargetDefID == LairDefID,
        "Zerg building morph records the Lair target def id");
    Require(HatcheryBuilding && HatcheryBuilding->UnderConstruction,
        "Zerg building morph puts the source building into construction state");
    Require(HatcheryBuilding && HatcheryBuilding->BuildProgress == 0.0f &&
            HatcheryBuilding->TotalBuildTime == (float)LairDef.BuildTime,
        "Zerg building morph progress starts empty and uses the target build time");
    auto* MorphStartRender =
        EM.GetComponent<Game::Components::RenderComponent>(MorphHatchery);
    Require(MorphStartRender &&
            MorphStartRender->ImageID == 50000u + (uint32_t)LairDef.ChkUnitID,
        "Zerg building morph immediately uses the target construction image");
    Require(ZergBuildingResources.Minerals == 500 - LairDef.MineralCost &&
            ZergBuildingResources.VespeneGas == 300 - LairDef.GasCost,
        "Zerg building morph spends normalized target costs");

    Game::Map::GameMap ZergMorphMap;
    Game::Systems::ZergSystem ZergMorphSystem(SysMgr, &ZergMorphMap);
    Game::Systems::BuildingSystem::SetMap(&ZergMorphMap);
    Game::Systems::CommandSystem::SetMap(&ZergMorphMap);
    Game::Systems::BuildingSystem::SetImageResolver(
        [](int ChkUnitID) { return 50000u + (uint32_t)ChkUnitID; });
    Game::Systems::BuildingSystem::SetConstructionImageResolver(
        [](int ChkUnitID) { return 50000u + (uint32_t)ChkUnitID; });
    Game::Systems::BuildingSystem ZergMorphBuildingSystem(SysMgr);
    Game::Systems::CommandSystem ZergMorphCommandSystem;
    ZergMorphCommandSystem.Init(&SysMgr);
    Game::Systems::MovementSystem ZergMorphMovementSystem(SysMgr, &ZergMorphMap);
    ZergMorphSystem.Update((float)LairDef.BuildTime * 0.5f);
    std::vector<int> HatchedZerglings;
    for (int EntityID : EM.GetEntitiesWith<Game::Components::UnitComponent,
                                           Game::Components::PlayerComponent>())
    {
        const auto* Unit = EM.GetComponent<Game::Components::UnitComponent>(EntityID);
        const auto* Player = EM.GetComponent<Game::Components::PlayerComponent>(EntityID);
        if (Unit && Player && Player->PlayerID == ZergMorphPlayerID &&
            Unit->ChkUnitID == ZerglingDef.ChkUnitID)
            HatchedZerglings.push_back(EntityID);
    }
    Require(HatchedZerglings.size() == 2 &&
            ZergMorphResources.SupplyUsed == ZerglingDef.SupplyCost,
        "one Zergling egg hatches a pair for one shared supply reservation");
    auto* FirstSharedSupply =
        EM.GetComponent<Game::Components::SharedSupplyComponent>(HatchedZerglings[0]);
    auto* SecondSharedSupply =
        EM.GetComponent<Game::Components::SharedSupplyComponent>(HatchedZerglings[1]);
    Require(FirstSharedSupply && SecondSharedSupply &&
            FirstSharedSupply->PartnerEntityID == HatchedZerglings[1] &&
            SecondSharedSupply->PartnerEntityID == HatchedZerglings[0],
        "paired Zerglings retain a symmetric shared-supply link");
    Game::Systems::RecordUnitDeath(EM, HatchedZerglings[0], -1, false);
    EM.DestroyEntity(HatchedZerglings[0]);
    Require(ZergMorphResources.SupplyUsed == ZerglingDef.SupplyCost,
        "the first paired Zergling death keeps supply reserved for its sibling");
    Game::Systems::RecordUnitDeath(EM, HatchedZerglings[1], -1, false);
    EM.DestroyEntity(HatchedZerglings[1]);
    Require(ZergMorphResources.SupplyUsed == 0,
        "the final paired Zergling death releases the shared supply exactly once");
    Require(HatcheryBuilding->BuildProgress > 0.0f &&
            HatcheryBuilding->BuildProgress < HatcheryBuilding->TotalBuildTime &&
            HatcheryBuilding->BuildTimeRemaining < HatcheryBuilding->TotalBuildTime,
        "Zerg building morph progress grows while remaining time decreases");
    const float HalfMorphProgress = HatcheryBuilding->BuildProgress;
    ZergMorphSystem.Update((float)LairDef.BuildTime * 0.5f + 0.1f);
    Require(HatcheryBuilding->BuildProgress >= HalfMorphProgress &&
            HatcheryBuilding->BuildProgress == HatcheryBuilding->TotalBuildTime,
        "completed Zerg building morph leaves a full forward progress bar");
    auto* MorphedLairUnit = EM.GetComponent<Game::Components::UnitComponent>(MorphHatchery);
    auto* MorphedLairRender = EM.GetComponent<Game::Components::RenderComponent>(MorphHatchery);
    Require(MorphedLairUnit && MorphedLairUnit->ChkUnitID == LairDef.ChkUnitID &&
            MorphedLairRender &&
            MorphedLairRender->ImageID == 50000u + (uint32_t)LairDef.ChkUnitID,
        "completed Zerg building morph switches to the target building image");
    Require(MorphedLairUnit->HitPoints ==
                LairDef.HitPoints - MorphingBuildingMissingHitPoints,
        "Hatchery to Lair morph preserves missing HP instead of healing for free");

    const int LairDroneLarva = CreateCompletedUnit(
        EM, "Larva", ZergBuildingMorphPlayerID, {912.0f, 552.0f});
    Require(Game::Systems::ZergSystem::StartMorphLarvaIntoUnit(
                EM, LairDroneLarva, Game::Races::GetUnitDefID("Drone")),
        "completed Lair satisfies the Hatchery prerequisite for larva production");

    const int HiveDefID = Game::Races::GetUnitDefID("Hive");
    Require(!Game::Systems::ZergSystem::CanMorphBuilding(
                EM, MorphHatchery, HiveDefID),
        "completed Lair cannot morph to Hive before Queen's Nest exists");
    CreateCompletedBuilding(
        EM, "Queen's Nest", ZergBuildingMorphPlayerID, {960.0f, 520.0f});
    const auto HiveDef = Game::Races::GetUnitDefByID(HiveDefID);
    Require(Game::Systems::ZergSystem::StartMorphBuilding(
                EM, MorphHatchery, HiveDefID),
        "completed Lair starts its second-tier Hive morph");
    Require(HatcheryBuilding->BuildProgress == 0.0f &&
            HatcheryBuilding->TotalBuildTime == (float)HiveDef.BuildTime &&
            MorphedLairRender->ImageID == 50000u + (uint32_t)HiveDef.ChkUnitID,
        "second-tier building morph restarts forward progress on the Hive construction image");
    ZergMorphSystem.Update((float)HiveDef.BuildTime * 0.5f);
    const float HalfHiveProgress = HatcheryBuilding->BuildProgress;
    Require(HalfHiveProgress > 0.0f &&
            HalfHiveProgress < HatcheryBuilding->TotalBuildTime,
        "Hive morph progress grows from empty at the halfway point");
    ZergMorphSystem.Update((float)HiveDef.BuildTime * 0.5f + 0.1f);
    Require(HatcheryBuilding->BuildProgress >= HalfHiveProgress &&
            HatcheryBuilding->BuildProgress == HatcheryBuilding->TotalBuildTime &&
            MorphedLairUnit->ChkUnitID == HiveDef.ChkUnitID,
        "second-tier Hive morph completes with a full forward progress bar");

    const int HiveDroneLarva = CreateCompletedUnit(
        EM, "Larva", ZergBuildingMorphPlayerID, {944.0f, 552.0f});
    Require(Game::Systems::ZergSystem::StartMorphLarvaIntoUnit(
                EM, HiveDroneLarva, Game::Races::GetUnitDefID("Drone")),
        "completed Hive satisfies the Hatchery prerequisite for larva production");

    constexpr int ZergLarvaEconomyPlayerID = 6;
    auto& ZergLarvaEconomyResources =
        Game::Systems::HarvestSystem::GetResources(ZergLarvaEconomyPlayerID);
    ZergLarvaEconomyResources.Minerals = 300;
    ZergLarvaEconomyResources.VespeneGas = 0;
    ZergLarvaEconomyResources.SupplyUsed = 8;
    ZergLarvaEconomyResources.SupplyMax = 9;

    const int EconomyHatchery =
        CreateCompletedBuilding(EM, "Hatchery", ZergLarvaEconomyPlayerID, {960.0f, 520.0f});
    Require(EconomyHatchery > 0, "completed Hatchery can support larva economy morphs");
    auto* EconomyHatcheryBuilding =
        EM.GetComponent<Game::Components::BuildingComponent>(EconomyHatchery);
    Require(EconomyHatcheryBuilding, "economy Hatchery has building state");
    EconomyHatcheryBuilding->IsOrganic = true;
    ZergMorphSystem.Update(15.1f);
    const int SpawnedLarva = FindUnitByDef(EM, "Larva", ZergLarvaEconomyPlayerID);
    auto* SpawnedLarvaRender = EM.GetComponent<Game::Components::RenderComponent>(SpawnedLarva);
    auto* SpawnedLarvaOwner = EM.GetComponent<Game::Components::LarvaComponent>(SpawnedLarva);
    Require(SpawnedLarva > 0 && SpawnedLarvaRender &&
            SpawnedLarvaRender->ImageID == 50000u + 0x23u,
        "Hatchery-spawned larva has a visible render image for IScript Init");
    Require(SpawnedLarvaOwner &&
            SpawnedLarvaOwner->ParentHatcheryEntityID == EconomyHatchery,
        "Hatchery-spawned larva is owned by the individual source Hatchery");
    const int DroneDefID = Game::Races::GetUnitDefID("Drone");
    const auto DroneDef = Game::Races::GetUnitDefByID(DroneDefID);
    const int DroneLarva =
        CreateCompletedUnit(EM, "Larva", ZergLarvaEconomyPlayerID, {992.0f, 520.0f});
    EM.AddComponent<Game::Components::LarvaComponent>(DroneLarva)
        .ParentHatcheryEntityID = EconomyHatchery;
    const int HatcheryExitBlocker =
        CreateCompletedUnit(EM, "Marine", 9, {960.0f, 558.0f});
    Require(Game::Systems::ZergSystem::CanMorphLarvaIntoUnit(EM, DroneLarva, DroneDefID),
        "Zerg larva can morph into Drone for worker economy closure");
    Require(Game::Systems::ZergSystem::StartMorphLarvaIntoUnit(EM, DroneLarva, DroneDefID),
        "Zerg Drone morph starts from larva");
    const int DroneEgg =
        FindMorphByTarget(EM, DroneDefID, ZergLarvaEconomyPlayerID);
    auto* DroneEggRender = EM.GetComponent<Game::Components::RenderComponent>(DroneEgg);
    Require(DroneEgg > 0 && DroneEggRender &&
            DroneEggRender->ImageID == 50000u + 0x24u,
        "Drone morph remains visible as an Egg while construction is active");
    Require(ZergLarvaEconomyResources.SupplyUsed == 9,
        "Zerg Drone morph reserves worker supply");
    ZergMorphSystem.Update((float)DroneDef.BuildTime + 0.1f);
    Require(CountUnitsByDef(EM, "Drone", ZergLarvaEconomyPlayerID) >= 1,
        "Zerg Drone morph completes into a worker unit");
    const int MorphedDrone = FindUnitByDef(EM, "Drone", ZergLarvaEconomyPlayerID);
    auto* MorphedDroneTransform =
        EM.GetComponent<Game::Components::TransformComponent>(MorphedDrone);
    auto* HatcheryExitBlockerTransform =
        EM.GetComponent<Game::Components::TransformComponent>(HatcheryExitBlocker);
    Require(MorphedDroneTransform && HatcheryExitBlockerTransform &&
            MorphedDroneTransform->Position.Y > 520.0f &&
            MorphedDroneTransform->Position.DistanceTo(
                HatcheryExitBlockerTransform->Position) >= 32.0f,
        "Zerg egg hatches below its parent base at a nearby unoccupied exit");
    auto* MorphedDroneRender = EM.GetComponent<Game::Components::RenderComponent>(MorphedDrone);
    Require(MorphedDroneRender &&
            MorphedDroneRender->ImageID == 50000u + (uint32_t)DroneDef.ChkUnitID,
        "completed Drone morph has the target unit image for IScript Init");

    ZergLarvaEconomyResources.Minerals = 300;
    const int OverlordDefID = Game::Races::GetUnitDefID("Overlord");
    const auto OverlordDef = Game::Races::GetUnitDefByID(OverlordDefID);
    const int OverlordLarva =
        CreateCompletedUnit(EM, "Larva", ZergLarvaEconomyPlayerID, {1024.0f, 520.0f});
    const int SupplyMaxBeforeOverlord = ZergLarvaEconomyResources.SupplyMax;
    Require(ZergLarvaEconomyResources.SupplyUsed == ZergLarvaEconomyResources.SupplyMax,
        "Zerg economy test reaches supply cap before Overlord morph");
    Require(Game::Systems::ZergSystem::CanMorphLarvaIntoUnit(EM, OverlordLarva, OverlordDefID),
        "Zerg larva can morph Overlord while supply capped");
    Require(Game::Systems::ZergSystem::StartMorphLarvaIntoUnit(EM, OverlordLarva, OverlordDefID),
        "Zerg Overlord morph starts from larva");
    const int OverlordEgg =
        FindMorphByTarget(EM, OverlordDefID, ZergLarvaEconomyPlayerID);
    auto* OverlordEggRender = EM.GetComponent<Game::Components::RenderComponent>(OverlordEgg);
    Require(OverlordEgg > 0 && OverlordEggRender &&
            OverlordEggRender->ImageID == 50000u + 0x24u,
        "Overlord morph uses the Egg image until completion");
    Require(ZergLarvaEconomyResources.SupplyUsed == 9,
        "Zerg Overlord morph does not consume supply");
    Require(ZergLarvaEconomyResources.SupplyMax == SupplyMaxBeforeOverlord,
        "Zerg Overlord supply is not granted before morph completion");
    ZergMorphSystem.Update((float)OverlordDef.BuildTime + 0.1f);
    Require(CountUnitsByDef(EM, "Overlord", ZergLarvaEconomyPlayerID) >= 1,
        "Zerg Overlord morph completes into an Overlord unit");
    const int MorphedOverlord =
        FindUnitByDef(EM, "Overlord", ZergLarvaEconomyPlayerID);
    auto* MorphedOverlordUnit =
        EM.GetComponent<Game::Components::UnitComponent>(MorphedOverlord);
    Require(MorphedOverlordUnit && MorphedOverlordUnit->IsDetector,
        "Zerg Overlord morph preserves detector metadata");
    auto* MorphedOverlordRender =
        EM.GetComponent<Game::Components::RenderComponent>(MorphedOverlord);
    Require(MorphedOverlordRender &&
            MorphedOverlordRender->ImageID == 50000u + (uint32_t)OverlordDef.ChkUnitID,
        "completed Overlord morph has the target unit image for IScript Init");
    Require(ZergLarvaEconomyResources.SupplyMax ==
            SupplyMaxBeforeOverlord + Game::Races::GetSupplyProvidedByDefID(OverlordDefID),
        "completed Zerg Overlord increases supply cap");

    const int LurkerLair = CreateCompletedBuilding(
        EM, "Lair", ZergLarvaEconomyPlayerID, {1120.0f, 520.0f});
    const int LurkerDen = CreateCompletedBuilding(
        EM, "Hydralisk Den", ZergLarvaEconomyPlayerID, {1180.0f, 520.0f});
    const int LurkerLarva = CreateCompletedUnit(
        EM, "Larva", ZergLarvaEconomyPlayerID, {1120.0f, 520.0f});
    EM.AddComponent<Game::Components::LarvaComponent>(LurkerLarva)
        .ParentHatcheryEntityID = LurkerLair;
    const int LurkerDefID = Game::Races::GetUnitDefID("Lurker");
    const auto LurkerDef = Game::Races::GetUnitDefByID(LurkerDefID);
    const int HydraliskDefID = Game::Races::GetUnitDefID("Hydralisk");
    const auto HydraliskDef = Game::Races::GetUnitDefByID(HydraliskDefID);
    Require(!Game::Systems::ZergSystem::CanMorphLarvaIntoUnit(
                EM, LurkerLarva, LurkerDefID),
        "Lurker cannot bypass its Hydralisk source by morphing from larva");

    const Core::Math::Vec2 LurkerMorphPosition{1216.0f, 552.0f};
    const int LurkerHydralisk = CreateCompletedUnit(
        EM, "Hydralisk", ZergLarvaEconomyPlayerID, LurkerMorphPosition);
    ZergLarvaEconomyResources.Minerals = 500;
    ZergLarvaEconomyResources.VespeneGas = 500;
    ZergLarvaEconomyResources.SupplyUsed = HydraliskDef.SupplyCost;
    ZergLarvaEconomyResources.SupplyMax = 30;
    Require(!Game::Systems::ZergSystem::CanMorphHydraliskIntoLurker(
                EM, LurkerHydralisk),
        "Hydralisk cannot morph before Lurker Aspect is researched");

    auto& LurkerUpgrades =
        Game::Systems::UpgradeSystem::GetUpgrades(ZergLarvaEconomyPlayerID);
    LurkerUpgrades.Levels[static_cast<size_t>(
        Game::Systems::ResearchId::Zerg_LurkerMorph)] = 1;
    EM.DestroyEntity(LurkerDen);
    EM.DestroyEntity(LurkerLair);
    Require(Game::Systems::ZergSystem::CanMorphHydraliskIntoLurker(
                EM, LurkerHydralisk),
        "researched Lurker Aspect remains usable after the Den and Lair are destroyed");

    // Probe both exact Brood War morph costs before allowing the conversion.
    ZergLarvaEconomyResources.Minerals = 49;
    Require(!Game::Systems::ZergSystem::CanMorphHydraliskIntoLurker(
                EM, LurkerHydralisk),
        "Lurker morph requires all 50 minerals");
    ZergLarvaEconomyResources.Minerals = 50;
    ZergLarvaEconomyResources.VespeneGas = 99;
    Require(!Game::Systems::ZergSystem::CanMorphHydraliskIntoLurker(
                EM, LurkerHydralisk),
        "Lurker morph requires all 100 vespene gas");
    ZergLarvaEconomyResources.VespeneGas = 100;
    ZergLarvaEconomyResources.SupplyMax = HydraliskDef.SupplyCost;
    Require(!Game::Systems::ZergSystem::CanMorphHydraliskIntoLurker(
                EM, LurkerHydralisk),
        "Lurker morph reserves only the supply increase over Hydralisk");
    ZergLarvaEconomyResources.SupplyMax = LurkerDef.SupplyCost;

    Require(Game::Systems::ZergSystem::StartMorphHydraliskIntoLurker(
                EM, LurkerHydralisk),
        "researched Hydralisk starts morphing into a Lurker Egg");
    Require(!EM.IsAlive(LurkerHydralisk),
        "Lurker morph consumes its source Hydralisk");
    Require(ZergLarvaEconomyResources.Minerals == 0 &&
            ZergLarvaEconomyResources.VespeneGas == 0,
        "Lurker morph deducts exactly 50 minerals and 100 gas");
    Require(ZergLarvaEconomyResources.SupplyUsed == LurkerDef.SupplyCost,
        "Lurker morph replaces Hydralisk supply instead of adding full Lurker supply");

    const int LurkerEgg =
        FindMorphByTarget(EM, LurkerDefID, ZergLarvaEconomyPlayerID);
    const auto* LurkerEggUnit =
        EM.GetComponent<Game::Components::UnitComponent>(LurkerEgg);
    const auto* LurkerEggBuilding =
        EM.GetComponent<Game::Components::BuildingComponent>(LurkerEgg);
    const auto* LurkerEggPath =
        EM.GetComponent<Game::Components::PathfindingComponent>(LurkerEgg);
    const auto* LurkerEggRender =
        EM.GetComponent<Game::Components::RenderComponent>(LurkerEgg);
    const auto* LurkerEggTransform =
        EM.GetComponent<Game::Components::TransformComponent>(LurkerEgg);
    Require(LurkerEgg > 0 && LurkerEggUnit &&
            LurkerEggUnit->ChkUnitID == 97 &&
            LurkerEggBuilding && LurkerEggBuilding->UnderConstruction &&
            LurkerEggPath && !LurkerEggPath->IsAir &&
            LurkerEggRender && LurkerEggRender->ImageID == 50000u + 97u,
        "Hydralisk morph uses the ground Lurker Egg intermediate form");
    Require(LurkerEggUnit->HitPoints == 200 &&
            LurkerEggUnit->MaxHitPoints == 200 &&
            LurkerEggUnit->Armor == 10,
        "Lurker Egg uses its independent classic 200 HP and 10 armor definition");
    Require(LurkerEggTransform &&
            LurkerEggTransform->Position.DistanceTo(LurkerMorphPosition) < 0.1f,
        "Lurker Egg remains at the consumed Hydralisk position");

    ZergMorphSystem.Update((float)LurkerDef.BuildTime + 0.1f);
    const int MorphedLurker =
        FindUnitByDef(EM, "Lurker", ZergLarvaEconomyPlayerID);
    const auto* LurkerBurrow =
        EM.GetComponent<Game::Components::BurrowComponent>(MorphedLurker);
    const auto* LurkerTransform =
        EM.GetComponent<Game::Components::TransformComponent>(MorphedLurker);
    const auto* LurkerPath =
        EM.GetComponent<Game::Components::PathfindingComponent>(MorphedLurker);
    const auto* LurkerCommand =
        EM.GetComponent<Game::Components::CommandComponent>(MorphedLurker);
    const auto* LurkerUnit =
        EM.GetComponent<Game::Components::UnitComponent>(MorphedLurker);
    Require(LurkerBurrow && LurkerBurrow->CanBurrow &&
            !LurkerBurrow->IsBurrowed &&
            LurkerBurrow->Phase == Game::Components::BurrowPhase::Surface,
        "newly hatched Lurker explicitly starts on the surface and can move");
    Require(LurkerTransform &&
            LurkerTransform->Position.DistanceTo(LurkerMorphPosition) < 0.1f &&
            LurkerPath && !LurkerPath->IsAir && LurkerCommand &&
            LurkerUnit && LurkerUnit->Speed > 0.0f,
        "completed Lurker is a movable ground unit at the morph position");
    Require(Game::Systems::ZergSystem::BurrowUnit(EM, MorphedLurker) &&
            LurkerBurrow->Phase == Game::Components::BurrowPhase::Burrowing,
        "completed Lurker can begin its innate Burrow transition");

    constexpr int ZergDroneBuildingPlayerID = 7;
    auto& ZergDroneBuildingResources =
        Game::Systems::HarvestSystem::GetResources(ZergDroneBuildingPlayerID);
    ZergDroneBuildingResources.Minerals = 500;
    ZergDroneBuildingResources.VespeneGas = 0;
    ZergDroneBuildingResources.SupplyUsed = 1;
    ZergDroneBuildingResources.SupplyMax = 10;

    const int BuilderDrone =
        CreateCompletedUnit(EM, "Drone", ZergDroneBuildingPlayerID, {1040.0f, 560.0f});
    const int HatcheryDefID = Game::Races::GetUnitDefID("Hatchery");
    const auto HatcheryDef = Game::Races::GetUnitDefByID(HatcheryDefID);
    Require(Game::Systems::ZergSystem::StartMorphDroneIntoBuilding(
                EM, BuilderDrone, HatcheryDefID, {2000.0f, 2000.0f}),
        "Zerg Drone can accept a distant Hatchery morph order");
    Require(EM.IsAlive(BuilderDrone) &&
            Game::Systems::BuildingSystem::CountPendingBuilds(
                ZergDroneBuildingPlayerID, HatcheryDefID) == 1,
        "Zerg Drone remains alive while moving to its Hatchery footprint");
    const int SpawningPoolDefID =
        Game::Races::GetUnitDefID("Spawning Pool");
    const auto* PendingDroneCommand =
        EM.GetComponent<Game::Components::CommandComponent>(BuilderDrone);
    std::string PendingDroneReason;
    Require(PendingDroneCommand &&
            PendingDroneCommand->CurrentCommand ==
                Game::Components::CommandType::Build &&
            PendingDroneCommand->GetCurrentOrder() &&
            PendingDroneCommand->GetCurrentOrder()->Type ==
                Game::Components::OrderType::Build &&
            !Game::Systems::ZergSystem::CanMorphDroneIntoBuilding(
                EM, BuilderDrone, SpawningPoolDefID, &PendingDroneReason) &&
            PendingDroneReason ==
                "drone is already committed to construction" &&
            !Game::Systems::ZergSystem::StartMorphDroneIntoBuilding(
                EM, BuilderDrone, SpawningPoolDefID, {1900.0f, 1900.0f}) &&
            PendingDroneCommand->CurrentCommand ==
                Game::Components::CommandType::Build &&
            Game::Systems::BuildingSystem::CountPendingBuilds(
                ZergDroneBuildingPlayerID, HatcheryDefID) == 1,
        "a second Zerg build request cannot steal or cancel an in-transit Drone");
    ZergMorphCommandSystem.Update(1.0f / 24.0f);
    ZergMorphMovementSystem.Update(1.0f / 24.0f);
    ZergMorphBuildingSystem.Update(0.001f);
    Require(FindBuildingByDef(
                EM, HatcheryDefID, ZergDroneBuildingPlayerID) < 0 &&
            EM.IsAlive(BuilderDrone),
        "Zerg Drone cannot remotely begin a building morph");
    auto* PendingDronePath =
        EM.GetComponent<Game::Components::PathfindingComponent>(BuilderDrone);
    Require(PendingDronePath,
        "in-transit Zerg builder keeps a construction approach path");
    EM.GetComponent<Game::Components::TransformComponent>(BuilderDrone)->Position =
        PendingDronePath->RequestedDestination;
    PendingDronePath->Path.clear();
    PendingDronePath->PathIndex = 0;
    ZergMorphCommandSystem.Update(1.0f / 24.0f);
    ZergMorphMovementSystem.Update(1.0f / 24.0f);
    ZergMorphBuildingSystem.Update(0.001f);
    Require(!EM.IsAlive(BuilderDrone),
        "Zerg Drone is consumed only after reaching the building footprint");
    const int MorphingHatchery =
        FindBuildingByDef(EM, HatcheryDefID, ZergDroneBuildingPlayerID);
    auto* MorphingHatcheryRender =
        EM.GetComponent<Game::Components::RenderComponent>(MorphingHatchery);
    Require(MorphingHatchery > 0 && MorphingHatcheryRender &&
            MorphingHatcheryRender->ImageID == 50000u + (uint32_t)HatcheryDef.ChkUnitID,
        "Drone building morph is visible with the target Zerg building image");
    Require(ZergDroneBuildingResources.SupplyUsed == 0,
        "Zerg Drone building morph releases the consumed Drone supply");
    Require(ZergDroneBuildingResources.SupplyMax == 10,
        "Zerg building morph does not grant supply before completion");
    const auto CountCreepNearNewHatchery = [&]()
    {
        int Count = 0;
        for (int Y = 56; Y <= 68; ++Y)
            for (int X = 52; X <= 72; ++X)
                if (ZergMorphMap.GetTile(X, Y).HasCreep)
                    ++Count;
        return Count;
    };
    ZergMorphBuildingSystem.Update(0.1f);
    ZergMorphSystem.Update(0.1f);
    Require(ZergMorphMap.GetTile(60, 61).HasCreep &&
            ZergMorphMap.GetTile(63, 63).HasCreep,
        "new Hatchery seeds continuous creep across its full 4x3 footprint");
    const int InitialConstructionCreep = CountCreepNearNewHatchery();
    ZergMorphBuildingSystem.Update(0.6f);
    ZergMorphSystem.Update(0.6f);
    Require(CountCreepNearNewHatchery() > InitialConstructionCreep,
        "Hatchery construction expands creep incrementally at the OpenBW spread interval");
    ZergMorphBuildingSystem.Update((float)HatcheryDef.BuildTime + 0.1f);
    ZergMorphSystem.Update(0.1f);
    Require(ZergDroneBuildingResources.SupplyMax ==
            10 + Game::Races::GetSupplyProvidedByDefID(HatcheryDefID),
        "completed Zerg Hatchery morph grants its supply cap");
    const int CompletedConstructionCreep = CountCreepNearNewHatchery();
    ZergMorphSystem.Update(2.0f);
    Require(CountCreepNearNewHatchery() == CompletedConstructionCreep,
        "completed Hatchery freezes its reached creep extent without further expansion");

    ZergDroneBuildingResources.Minerals = HatcheryDef.MineralCost + 100;
    const int UserCancelledDrone = CreateCompletedUnit(
        EM, "Drone", ZergDroneBuildingPlayerID, {1280.0f, 800.0f});
    const int MineralsBeforeUserCancelledBuild =
        ZergDroneBuildingResources.Minerals;
    Require(Game::Systems::ZergSystem::StartMorphDroneIntoBuilding(
                EM, UserCancelledDrone, HatcheryDefID, {2300.0f, 2000.0f}),
        "a second Drone can start a pending build for replacement-command coverage");
    Game::Systems::CommandSystem::IssueMove(
        EM, {UserCancelledDrone}, {1400.0f, 800.0f});
    ZergMorphBuildingSystem.Update(0.001f);
    const auto* UserCancelledCommand =
        EM.GetComponent<Game::Components::CommandComponent>(UserCancelledDrone);
    Require(UserCancelledCommand && UserCancelledCommand->GetCurrentOrder() &&
            UserCancelledCommand->GetCurrentOrder()->Type ==
                Game::Components::OrderType::Move &&
            Game::Systems::BuildingSystem::CountPendingBuilds(
                ZergDroneBuildingPlayerID, HatcheryDefID) == 0 &&
            ZergDroneBuildingResources.Minerals ==
                MineralsBeforeUserCancelledBuild,
        "a real user Move cancels pending construction, refunds it, and preserves the new order");

    ZergDroneBuildingResources.Minerals = HatcheryDef.MineralCost + 100;
    const int TransferredDrone = CreateCompletedUnit(
        EM, "Drone", ZergDroneBuildingPlayerID, {1280.0f, 896.0f});
    const int MineralsBeforeTransferredBuild =
        ZergDroneBuildingResources.Minerals;
    Require(Game::Systems::ZergSystem::StartMorphDroneIntoBuilding(
                EM, TransferredDrone, HatcheryDefID, {2300.0f, 2200.0f}),
        "Drone can start a pending build before a trigger ownership transfer");
    EM.GetComponent<Game::Components::PlayerComponent>(TransferredDrone)->PlayerID = 8;
    ZergMorphBuildingSystem.Update(0.001f);
    const auto* TransferredCommand =
        EM.GetComponent<Game::Components::CommandComponent>(TransferredDrone);
    Require(EM.IsAlive(TransferredDrone) && TransferredCommand &&
            TransferredCommand->CurrentCommand ==
                Game::Components::CommandType::Stop &&
            Game::Systems::BuildingSystem::CountPendingBuilds(
                ZergDroneBuildingPlayerID, HatcheryDefID) == 0 &&
            ZergDroneBuildingResources.Minerals ==
                MineralsBeforeTransferredBuild,
        "ownership transfer cancels and refunds the old pending build without consuming the Drone");

    constexpr int ProtossMorphPlayerID = 1;
    auto& ProtossMorphResources =
        Game::Systems::HarvestSystem::GetResources(ProtossMorphPlayerID);
    ProtossMorphResources.Minerals = 0;
    ProtossMorphResources.VespeneGas = 0;
    ProtossMorphResources.SupplyUsed = 4;
    ProtossMorphResources.SupplyMax = 20;

    const int TemplarA =
        CreateCompletedUnit(EM, "High Templar", ProtossMorphPlayerID, {1200.0f, 600.0f});
    const int TemplarB =
        CreateCompletedUnit(EM, "High Templar", ProtossMorphPlayerID, {1232.0f, 600.0f});
    const int ArchonDefID = Game::Races::GetUnitDefID("Archon");
    const auto ArchonDef = Game::Races::GetUnitDefByID(ArchonDefID);
    Require(!Game::Systems::ProtossSystem::MorphArchon(EM, TemplarA, TemplarA, false),
        "Protoss Archon morph rejects using the same source unit twice");
    Require(EM.IsAlive(TemplarA) && EM.IsAlive(TemplarB),
        "rejected Archon morph preserves both source units");
    const int InvalidArchonSource =
        CreateCompletedUnit(EM, "SCV", ProtossMorphPlayerID, {1264.0f, 600.0f});
    Require(!Game::Systems::ProtossSystem::MorphArchon(
                EM, TemplarA, InvalidArchonSource, false),
        "Protoss Archon morph rejects non-High-Templar source units");
    Require(EM.IsAlive(TemplarA) && EM.IsAlive(InvalidArchonSource),
        "wrong-type Archon morph preserves its source units");
    Game::Systems::ProductionSystem::SetImageResolver(
        [](int ChkUnitID) { return 60000u + (uint32_t)ChkUnitID; });
    Game::Systems::ProductionSystem::SetCollisionBoundsResolver(
        [](int) {
            return Game::Components::UnitCollisionBounds{true, 11, 12, 13, 14};
        });
    Require(Game::Systems::ProtossSystem::MorphArchon(EM, TemplarA, TemplarB, false),
        "Protoss High Templar pair can morph into Archon");
    Require(!EM.IsAlive(TemplarA) && !EM.IsAlive(TemplarB),
        "Protoss Archon morph consumes both source templars");
    Require(ProtossMorphResources.SupplyUsed == 4,
        "Protoss Archon morph preserves the combined templar supply reservation");

    const int Archon = FindUnitByDef(EM, "Archon", ProtossMorphPlayerID);
    Require(Archon > 0, "Protoss Archon morph creates an Archon entity");
    auto* ArchonUnit = EM.GetComponent<Game::Components::UnitComponent>(Archon);
    auto* ArchonRender = EM.GetComponent<Game::Components::RenderComponent>(Archon);
    auto* ArchonPath = EM.GetComponent<Game::Components::PathfindingComponent>(Archon);
    auto* ArchonCommand = EM.GetComponent<Game::Components::CommandComponent>(Archon);
    auto* ArchonCombat = EM.GetComponent<Game::Components::CombatComponent>(Archon);
    auto* ArchonTransform = EM.GetComponent<Game::Components::TransformComponent>(Archon);
    Require(ArchonUnit &&
            ArchonUnit->ChkUnitID == ArchonDef.ChkUnitID &&
            ArchonUnit->ShieldPoints == ArchonDef.ShieldPoints &&
            ArchonUnit->SupplyCost == ArchonDef.SupplyCost,
        "Protoss Archon morph initializes unit data from RaceDef");
    Require(ArchonRender &&
            ArchonRender->ImageID == 60000u + (uint32_t)ArchonDef.ChkUnitID &&
            ArchonRender->Visible,
        "Protoss Archon morph uses the configured production image resolver");
    Require(ArchonPath && !ArchonPath->IsAir && ArchonPath->NodeTolerance == 8.0f &&
            ArchonPath->HasCollisionBounds &&
            ArchonPath->CollisionLeft == 11 && ArchonPath->CollisionUp == 12 &&
            ArchonPath->CollisionRight == 13 && ArchonPath->CollisionDown == 14,
        "Protoss Archon morph applies production collision bounds");
    Require(ArchonCommand &&
            ArchonCommand->CurrentCommand == Game::Components::CommandType::Stop,
        "Protoss Archon morph creates command state");
    Require(ArchonCombat &&
            ArchonCombat->CanTargetGround &&
            ArchonCombat->CanTargetAir &&
            ArchonCombat->GroundDamage == ArchonDef.GroundDamage &&
            ArchonCombat->AirDamage == ArchonDef.AirDamage,
        "Protoss Archon morph creates combat state from RaceDef");
    Require(ArchonTransform &&
            ArchonTransform->Position.X == 1216.0f &&
            ArchonTransform->Position.Y == 600.0f,
        "Protoss Archon morph spawns at the midpoint between source templars");

    // Test channeled Archon warp and charging shields
    Game::Systems::ProtossSystem ProtossSys(SysMgr, nullptr);
    const int TemplarC =
        CreateCompletedUnit(EM, "High Templar", ProtossMorphPlayerID, {1000.0f, 600.0f});
    const int TemplarD =
        CreateCompletedUnit(EM, "High Templar", ProtossMorphPlayerID, {1040.0f, 600.0f});
    Require(Game::Systems::ProtossSystem::StartArchonWarp(
                EM, TemplarC, TemplarD, false, 2.0f, true),
        "Protoss High Templar pair starts channeled Archon warp");
    auto* WarpC = EM.GetComponent<Game::Components::ArchonWarpComponent>(TemplarC);
    auto* WarpD = EM.GetComponent<Game::Components::ArchonWarpComponent>(TemplarD);
    Require(WarpC && WarpD && WarpC->IsWarping && WarpD->IsWarping,
        "both templars enter warping state");

    // Partial update: channeling templars still alive
    ProtossSys.Update(1.0f);
    Require(EM.IsAlive(TemplarC) && EM.IsAlive(TemplarD),
        "channeling templars survive partial warp time");

    // Finish update: warp finishes and produces Archon with low initial shields charging up
    EM.DestroyEntity(Archon);
    ProtossSys.Update(1.1f);
    Require(!EM.IsAlive(TemplarC) && !EM.IsAlive(TemplarD),
        "completed channeled Archon warp consumes both source templars");
    const int ChanneledArchon = FindUnitByDef(EM, "Archon", ProtossMorphPlayerID);
    Require(ChanneledArchon > 0, "Channeled Archon entity spawned upon warp completion");
    auto* ChanneledArchonUnit = EM.GetComponent<Game::Components::UnitComponent>(ChanneledArchon);
    Require(ChanneledArchonUnit && ChanneledArchonUnit->ShieldPoints == 10,
        "Channeled Archon spawns with initial 10 shields charging up");

    constexpr int AdvancedTechPlayerID = 12;
    auto& AdvancedTechResources =
        Game::Systems::HarvestSystem::GetResources(AdvancedTechPlayerID);
    AdvancedTechResources.Minerals = 5000;
    AdvancedTechResources.VespeneGas = 5000;
    AdvancedTechResources.SupplyUsed = 0;
    AdvancedTechResources.SupplyMax = 200;

    const int BattlecruiserDefID = Game::Races::GetUnitDefID("Battlecruiser");
    const int BattlecruiserStarport = CreateCompletedBuilding(
        EM, "Starport", AdvancedTechPlayerID, {1400.0f, 700.0f});
    Require(!Game::Systems::ProductionSystem::CanTrain(
                EM, BattlecruiserStarport, BattlecruiserDefID),
        "Battlecruiser is blocked before the Starport has a Control Tower");
    const int BattlecruiserControlTower = CreateCompletedBuilding(
        EM, "ControlTower", AdvancedTechPlayerID, {1448.0f, 700.0f});
    auto* BattlecruiserStarportBuilding =
        EM.GetComponent<Game::Components::BuildingComponent>(BattlecruiserStarport);
    Require(BattlecruiserStarportBuilding,
        "Battlecruiser prerequisite probe has Starport building state");
    BattlecruiserStarportBuilding->AddonID = BattlecruiserControlTower;
    CreateCompletedBuilding(
        EM, "ScienceFacility", AdvancedTechPlayerID, {1500.0f, 700.0f});
    Require(!Game::Systems::ProductionSystem::CanTrain(
                EM, BattlecruiserStarport, BattlecruiserDefID),
        "Control Tower and Science Facility alone do not unlock Battlecruiser");
    CreateCompletedBuilding(
        EM, "PhysicsLab", AdvancedTechPlayerID, {1548.0f, 700.0f});
    Require(Game::Systems::ProductionSystem::CanTrain(
                EM, BattlecruiserStarport, BattlecruiserDefID),
        "Control Tower and Physics Lab complete the Battlecruiser technology chain");

    const int ObserverDefID = Game::Races::GetUnitDefID("Observer");
    const int RoboticsFacility = CreateCompletedBuilding(
        EM, "Robotics Facility", AdvancedTechPlayerID, {1600.0f, 700.0f});
    Require(!Game::Systems::ProductionSystem::CanTrain(
                EM, RoboticsFacility, ObserverDefID),
        "Observer is blocked before an Observatory exists");
    CreateCompletedBuilding(
        EM, "Observatory", AdvancedTechPlayerID, {1648.0f, 700.0f});
    Require(Game::Systems::ProductionSystem::CanTrain(
                EM, RoboticsFacility, ObserverDefID),
        "Robotics Facility and Observatory unlock Observer production");

    const int CarrierDefID = Game::Races::GetUnitDefID("Carrier");
    const int CarrierStargate = CreateCompletedBuilding(
        EM, "Stargate", AdvancedTechPlayerID, {1700.0f, 700.0f});
    Require(!Game::Systems::ProductionSystem::CanTrain(
                EM, CarrierStargate, CarrierDefID),
        "Carrier is blocked before a Fleet Beacon exists");
    CreateCompletedBuilding(
        EM, "Fleet Beacon", AdvancedTechPlayerID, {1748.0f, 700.0f});
    Require(Game::Systems::ProductionSystem::CanTrain(
                EM, CarrierStargate, CarrierDefID),
        "Stargate and Fleet Beacon unlock Carrier production");
    Require(Game::Systems::ProductionSystem::StartTrain(
                EM, CarrierStargate, CarrierDefID),
        "Carrier can enter the Stargate production queue");
    const auto CarrierDef = Game::Races::GetUnitDefByID(CarrierDefID);
    Production.Update((float)CarrierDef.BuildTime + 0.1f);
    const int TrainedCarrier =
        FindUnitByDef(EM, "Carrier", AdvancedTechPlayerID);
    const auto* TrainedCarrierUnit =
        EM.GetComponent<Game::Components::UnitComponent>(TrainedCarrier);
    Require(TrainedCarrier > 0 && TrainedCarrierUnit &&
            TrainedCarrierUnit->HangarCount == 0 &&
            !TrainedCarrierUnit->HangarBuildInProgress,
        "newly trained Carrier leaves the Stargate with zero interceptors");

    // Upgrade prerequisites and timers follow the Brood War DAT/OpenBW rules,
    // independently of the production-tech coverage above.
    Core::ECS::SystemManager UpgradeRuleSysMgr;
    auto& UpgradeRuleEM = UpgradeRuleSysMgr.GetEntityManager();
    constexpr int TerranUpgradeRulePlayerID = 9;
    constexpr int ZergUpgradeRulePlayerID = 10;
    constexpr int ProtossUpgradeRulePlayerID = 11;

    const auto SavedTerranUpgradeRules =
        Game::Systems::UpgradeSystem::GetUpgrades(TerranUpgradeRulePlayerID);
    const auto SavedZergUpgradeRules =
        Game::Systems::UpgradeSystem::GetUpgrades(ZergUpgradeRulePlayerID);
    const auto SavedProtossUpgradeRules =
        Game::Systems::UpgradeSystem::GetUpgrades(ProtossUpgradeRulePlayerID);
    const auto SavedTerranUpgradeResources =
        Game::Systems::HarvestSystem::GetResources(TerranUpgradeRulePlayerID);
    const auto SavedZergUpgradeResources =
        Game::Systems::HarvestSystem::GetResources(ZergUpgradeRulePlayerID);
    const auto SavedProtossUpgradeResources =
        Game::Systems::HarvestSystem::GetResources(ProtossUpgradeRulePlayerID);

    auto& TerranUpgradeRules =
        Game::Systems::UpgradeSystem::GetUpgrades(TerranUpgradeRulePlayerID);
    TerranUpgradeRules.Levels.fill(0);
    auto& TerranUpgradeResources =
        Game::Systems::HarvestSystem::GetResources(TerranUpgradeRulePlayerID);
    TerranUpgradeResources.Minerals = 5000;
    TerranUpgradeResources.VespeneGas = 5000;
    const int RuleEngineeringBay = CreateCompletedBuilding(
        UpgradeRuleEM, "EngineeringBay", TerranUpgradeRulePlayerID);
    TerranUpgradeRules.Levels[static_cast<size_t>(
        Game::Systems::ResearchId::Terran_InfantryWeapons)] = 1;
    Require(!Game::Systems::UpgradeSystem::CanStartResearch(
                UpgradeRuleEM, RuleEngineeringBay,
                Game::Systems::ResearchId::Terran_InfantryWeapons),
        "Terran level-two infantry upgrade requires a completed Science Facility");
    const int RuleScienceFacility = CreateCompletedBuilding(
        UpgradeRuleEM, "ScienceFacility", TerranUpgradeRulePlayerID);
    auto* RuleScienceFacilityBuilding =
        UpgradeRuleEM.GetComponent<Game::Components::BuildingComponent>(
            RuleScienceFacility);
    Require(RuleScienceFacilityBuilding != nullptr,
        "Terran upgrade prerequisite has building state");
    RuleScienceFacilityBuilding->UnderConstruction = true;
    Require(!Game::Systems::UpgradeSystem::CanStartResearch(
                UpgradeRuleEM, RuleEngineeringBay,
                Game::Systems::ResearchId::Terran_InfantryWeapons),
        "an unfinished Science Facility does not unlock Terran level-two upgrades");
    RuleScienceFacilityBuilding->UnderConstruction = false;
    Require(Game::Systems::UpgradeSystem::StartResearch(
                UpgradeRuleEM, RuleEngineeringBay,
                Game::Systems::ResearchId::Terran_InfantryWeapons),
        "completed Science Facility unlocks Terran level-two infantry upgrade");
    auto* RuleEngineeringBayBuilding =
        UpgradeRuleEM.GetComponent<Game::Components::BuildingComponent>(
            RuleEngineeringBay);
    Require(RuleEngineeringBayBuilding &&
            std::abs(RuleEngineeringBayBuilding->BuildTimeRemaining -
                     4480.0f / 24.0f) < 0.0001f,
        "level-two core upgrade uses the exact 4480-frame DAT duration");
    Require(Game::Systems::UpgradeSystem::GetResearchTimeCost(
                Game::Systems::ResearchId::Terran_InfantryWeapons,
                TerranUpgradeRulePlayerID) == 187,
        "whole-second research metadata rounds the exact level-two duration up");
    Require(Game::Systems::UpgradeSystem::CancelResearch(
                UpgradeRuleEM, RuleEngineeringBay),
        "Terran prerequisite timer probe cancels cleanly");

    const int RuleAcademy = CreateCompletedBuilding(
        UpgradeRuleEM, "Academy", TerranUpgradeRulePlayerID);
    Require(Game::Systems::UpgradeSystem::StartResearch(
                UpgradeRuleEM, RuleAcademy,
                Game::Systems::ResearchId::Terran_StimPacks),
        "Stim Packs research starts for exact tech timer coverage");
    auto* RuleAcademyBuilding =
        UpgradeRuleEM.GetComponent<Game::Components::BuildingComponent>(RuleAcademy);
    Require(RuleAcademyBuilding &&
            std::abs(RuleAcademyBuilding->BuildTimeRemaining -
                     1200.0f / 24.0f) < 0.0001f,
        "Stim Packs uses the exact 1200-frame techdata duration");
    Require(Game::Systems::UpgradeSystem::CancelResearch(UpgradeRuleEM, RuleAcademy),
        "Stim Packs timer probe cancels cleanly");

    const int RuleMachineShop = CreateCompletedBuilding(
        UpgradeRuleEM, "MachineShop", TerranUpgradeRulePlayerID);
    Require(!Game::Systems::UpgradeSystem::CanStartResearch(
                UpgradeRuleEM, RuleMachineShop,
                Game::Systems::ResearchId::Terran_CharonBoosters),
        "Charon Boosters remains locked without an Armory");
    CreateCompletedBuilding(UpgradeRuleEM, "Armory", TerranUpgradeRulePlayerID);
    Require(Game::Systems::UpgradeSystem::CanStartResearch(
                UpgradeRuleEM, RuleMachineShop,
                Game::Systems::ResearchId::Terran_CharonBoosters),
        "completed Armory unlocks Charon Boosters");

    auto& ZergUpgradeRules =
        Game::Systems::UpgradeSystem::GetUpgrades(ZergUpgradeRulePlayerID);
    ZergUpgradeRules.Levels.fill(0);
    auto& ZergUpgradeResources =
        Game::Systems::HarvestSystem::GetResources(ZergUpgradeRulePlayerID);
    ZergUpgradeResources.Minerals = 5000;
    ZergUpgradeResources.VespeneGas = 5000;
    const int RuleEvolutionChamber = CreateCompletedBuilding(
        UpgradeRuleEM, "Evolution Chamber", ZergUpgradeRulePlayerID);
    const int RuleSpawningPool = CreateCompletedBuilding(
        UpgradeRuleEM, "Spawning Pool", ZergUpgradeRulePlayerID);
    const int RuleHydraliskDen = CreateCompletedBuilding(
        UpgradeRuleEM, "Hydralisk Den", ZergUpgradeRulePlayerID);
    ZergUpgradeRules.Levels[static_cast<size_t>(
        Game::Systems::ResearchId::Zerg_MeleeAttacks)] = 1;
    Require(!Game::Systems::UpgradeSystem::CanStartResearch(
                UpgradeRuleEM, RuleEvolutionChamber,
                Game::Systems::ResearchId::Zerg_MeleeAttacks),
        "Zerg level-two attack upgrade is locked before Lair technology");
    Require(!Game::Systems::UpgradeSystem::CanStartResearch(
                UpgradeRuleEM, RuleSpawningPool,
                Game::Systems::ResearchId::Zerg_AdrenalGlands),
        "Adrenal Glands requires a completed Hive");
    Require(!Game::Systems::UpgradeSystem::CanStartResearch(
                UpgradeRuleEM, RuleHydraliskDen,
                Game::Systems::ResearchId::Zerg_LurkerMorph),
        "Lurker Aspect requires a completed Lair or Hive");

    const int RuleHive = CreateCompletedBuilding(
        UpgradeRuleEM, "Hive", ZergUpgradeRulePlayerID);
    auto* RuleHiveBuilding =
        UpgradeRuleEM.GetComponent<Game::Components::BuildingComponent>(RuleHive);
    Require(RuleHiveBuilding != nullptr, "Zerg Hive prerequisite has building state");
    RuleHiveBuilding->UnderConstruction = true;
    Require(Game::Systems::UpgradeSystem::CanStartResearch(
                UpgradeRuleEM, RuleEvolutionChamber,
                Game::Systems::ResearchId::Zerg_MeleeAttacks),
        "an owned morphing Hive preserves the OpenBW level-two Zerg prerequisite");
    ZergUpgradeRules.Levels[static_cast<size_t>(
        Game::Systems::ResearchId::Zerg_MeleeAttacks)] = 2;
    Require(!Game::Systems::UpgradeSystem::CanStartResearch(
                UpgradeRuleEM, RuleEvolutionChamber,
                Game::Systems::ResearchId::Zerg_MeleeAttacks),
        "Zerg level-three attack upgrade waits for Hive completion");
    Require(!Game::Systems::UpgradeSystem::CanStartResearch(
                UpgradeRuleEM, RuleSpawningPool,
                Game::Systems::ResearchId::Zerg_AdrenalGlands),
        "a morphing Hive does not prematurely unlock Adrenal Glands");
    CreateCompletedBuilding(UpgradeRuleEM, "Lair", ZergUpgradeRulePlayerID);
    Require(Game::Systems::UpgradeSystem::CanStartResearch(
                UpgradeRuleEM, RuleHydraliskDen,
                Game::Systems::ResearchId::Zerg_LurkerMorph),
        "completed Lair unlocks Lurker Aspect");
    RuleHiveBuilding->UnderConstruction = false;
    Require(Game::Systems::UpgradeSystem::CanStartResearch(
                UpgradeRuleEM, RuleEvolutionChamber,
                Game::Systems::ResearchId::Zerg_MeleeAttacks),
        "completed Hive unlocks Zerg level-three attack upgrade");
    Require(Game::Systems::UpgradeSystem::CanStartResearch(
                UpgradeRuleEM, RuleSpawningPool,
                Game::Systems::ResearchId::Zerg_AdrenalGlands),
        "completed Hive unlocks Adrenal Glands");

    auto& ProtossUpgradeRules =
        Game::Systems::UpgradeSystem::GetUpgrades(ProtossUpgradeRulePlayerID);
    ProtossUpgradeRules.Levels.fill(0);
    auto& ProtossUpgradeResources =
        Game::Systems::HarvestSystem::GetResources(ProtossUpgradeRulePlayerID);
    ProtossUpgradeResources.Minerals = 5000;
    ProtossUpgradeResources.VespeneGas = 5000;
    const int RuleForge = CreateCompletedBuilding(
        UpgradeRuleEM, "Forge", ProtossUpgradeRulePlayerID);
    ProtossUpgradeRules.Levels[static_cast<size_t>(
        Game::Systems::ResearchId::Protoss_GroundWeapons)] = 1;
    ProtossUpgradeRules.Levels[static_cast<size_t>(
        Game::Systems::ResearchId::Protoss_PlasmaShields)] = 1;
    Require(!Game::Systems::UpgradeSystem::CanStartResearch(
                UpgradeRuleEM, RuleForge,
                Game::Systems::ResearchId::Protoss_GroundWeapons),
        "Protoss level-two ground upgrade requires Templar Archives");
    Require(!Game::Systems::UpgradeSystem::CanStartResearch(
                UpgradeRuleEM, RuleForge,
                Game::Systems::ResearchId::Protoss_PlasmaShields),
        "Protoss level-two shield upgrade requires Cybernetics Core");
    const int RuleCyberneticsCore = CreateCompletedBuilding(
        UpgradeRuleEM, "Cybernetics Core", ProtossUpgradeRulePlayerID);
    Require(Game::Systems::UpgradeSystem::CanStartResearch(
                UpgradeRuleEM, RuleForge,
                Game::Systems::ResearchId::Protoss_PlasmaShields),
        "completed Cybernetics Core unlocks Protoss level-two shields");
    ProtossUpgradeRules.Levels[static_cast<size_t>(
        Game::Systems::ResearchId::Protoss_AirWeapons)] = 1;
    Require(!Game::Systems::UpgradeSystem::CanStartResearch(
                UpgradeRuleEM, RuleCyberneticsCore,
                Game::Systems::ResearchId::Protoss_AirWeapons),
        "Protoss level-two air upgrade requires Fleet Beacon");
    CreateCompletedBuilding(
        UpgradeRuleEM, "Templar Archives", ProtossUpgradeRulePlayerID);
    Require(Game::Systems::UpgradeSystem::CanStartResearch(
                UpgradeRuleEM, RuleForge,
                Game::Systems::ResearchId::Protoss_GroundWeapons),
        "completed Templar Archives unlocks Protoss level-two ground upgrades");
    const int RuleFleetBeacon = CreateCompletedBuilding(
        UpgradeRuleEM, "Fleet Beacon", ProtossUpgradeRulePlayerID);
    Require(Game::Systems::UpgradeSystem::CanStartResearch(
                UpgradeRuleEM, RuleCyberneticsCore,
                Game::Systems::ResearchId::Protoss_AirWeapons),
        "completed Fleet Beacon unlocks Protoss level-two air upgrades");
    Require(Game::Systems::UpgradeSystem::CanStartResearch(
                UpgradeRuleEM, RuleFleetBeacon,
                Game::Systems::ResearchId::Protoss_GraviticThrusters),
        "Fleet Beacon exposes Gravitic Thrusters research");
    Require(Game::Systems::UpgradeSystem::StartResearch(
                UpgradeRuleEM, RuleFleetBeacon,
                Game::Systems::ResearchId::Protoss_DisruptionWeb),
        "Disruption Web has complete research metadata");
    auto* RuleFleetBeaconBuilding =
        UpgradeRuleEM.GetComponent<Game::Components::BuildingComponent>(
            RuleFleetBeacon);
    Require(RuleFleetBeaconBuilding &&
            std::abs(RuleFleetBeaconBuilding->BuildTimeRemaining -
                     1200.0f / 24.0f) < 0.0001f,
        "Disruption Web uses the exact 1200-frame techdata duration");
    Require(Game::Systems::UpgradeSystem::CancelResearch(
                UpgradeRuleEM, RuleFleetBeacon),
        "Disruption Web timer probe cancels cleanly");
    const int RuleObservatory = CreateCompletedBuilding(
        UpgradeRuleEM, "Observatory", ProtossUpgradeRulePlayerID);
    Require(Game::Systems::UpgradeSystem::CanStartResearch(
                UpgradeRuleEM, RuleObservatory,
                Game::Systems::ResearchId::Protoss_GraviticBoosters),
        "Observatory exposes Gravitic Boosters research");

    Game::Systems::UpgradeSystem::GetUpgrades(TerranUpgradeRulePlayerID) =
        SavedTerranUpgradeRules;
    Game::Systems::UpgradeSystem::GetUpgrades(ZergUpgradeRulePlayerID) =
        SavedZergUpgradeRules;
    Game::Systems::UpgradeSystem::GetUpgrades(ProtossUpgradeRulePlayerID) =
        SavedProtossUpgradeRules;
    Game::Systems::HarvestSystem::GetResources(TerranUpgradeRulePlayerID) =
        SavedTerranUpgradeResources;
    Game::Systems::HarvestSystem::GetResources(ZergUpgradeRulePlayerID) =
        SavedZergUpgradeResources;
    Game::Systems::HarvestSystem::GetResources(ProtossUpgradeRulePlayerID) =
        SavedProtossUpgradeResources;

    constexpr int HangarEconomyPlayerID = 9;
    auto& HangarUpgrades =
        Game::Systems::UpgradeSystem::GetUpgrades(HangarEconomyPlayerID);
    HangarUpgrades.Levels[static_cast<size_t>(
        Game::Systems::ResearchId::Protoss_CarrierCapacity)] = 0;
    HangarUpgrades.Levels[static_cast<size_t>(
        Game::Systems::ResearchId::Protoss_ReaverCapacity)] = 0;
    auto& HangarResources =
        Game::Systems::HarvestSystem::GetResources(HangarEconomyPlayerID);
    HangarResources.Minerals = 24;
    HangarResources.VespeneGas = 0;

    const int EconomyCarrier = Game::Systems::ProductionSystem::DebugSpawnUnit(
        EM,
        Game::Races::GetUnitDefID("Carrier"),
        {224.0f, 128.0f},
        HangarEconomyPlayerID);
    auto* EconomyCarrierUnit =
        EM.GetComponent<Game::Components::UnitComponent>(EconomyCarrier);
    Require(EconomyCarrierUnit && EconomyCarrierUnit->HangarCount == 0 &&
            !EconomyCarrierUnit->HangarBuildInProgress,
        "new Carrier starts with an empty interceptor hangar");
    Require(EconomyCarrierUnit->ChkUnitID == 72 &&
            Game::Systems::UpgradeSystem::GetMaxHangarCount(
                EM, EconomyCarrier) == 4,
        "Carrier payload economy recognizes the classic unit id and base capacity");
    Production.Update(1.0f);
    Require(HangarResources.Minerals == 24 &&
            !EconomyCarrierUnit->HangarBuildInProgress,
        "Carrier cannot begin an interceptor without its 25 mineral cost");

    // A CHK placement can author a partial payload. Automatic construction
    // must continue from that exact count rather than resetting it.
    EconomyCarrierUnit->HangarCount = 3;
    HangarResources.Minerals = 100;
    Production.Update(19.0f);
    Require(EconomyCarrierUnit->HangarCount == 3,
        "Carrier preserves map-authored payload while building an interceptor");
    Require(EconomyCarrierUnit->HangarBuildInProgress,
        "Carrier keeps interceptor construction active before 20 seconds");
    Require(HangarResources.Minerals == 75,
        "Carrier reserves 25 minerals when interceptor construction begins");
    Production.Update(1.0f);
    Require(EconomyCarrierUnit->HangarCount == 4 &&
            !EconomyCarrierUnit->HangarBuildInProgress &&
            HangarResources.Minerals == 75,
        "Carrier completes exactly one interceptor after 20 seconds");
    Production.Update(60.0f);
    Require(EconomyCarrierUnit->HangarCount == 4 &&
            HangarResources.Minerals == 75,
        "base Carrier capacity stops automatic construction at four");

    HangarUpgrades.Levels[static_cast<size_t>(
        Game::Systems::ResearchId::Protoss_CarrierCapacity)] = 1;
    Production.Update(20.0f);
    Require(EconomyCarrierUnit->HangarCount == 5 &&
            HangarResources.Minerals == 50,
        "Carrier Capacity permits another paid 20-second interceptor build");
    EconomyCarrierUnit->HangarCount = 8;

    const int EconomyReaver = Game::Systems::ProductionSystem::DebugSpawnUnit(
        EM,
        Game::Races::GetUnitDefID("Reaver"),
        {256.0f, 128.0f},
        HangarEconomyPlayerID);
    auto* EconomyReaverUnit =
        EM.GetComponent<Game::Components::UnitComponent>(EconomyReaver);
    Require(EconomyReaverUnit && EconomyReaverUnit->HangarCount == 0,
        "new Reaver starts with an empty scarab bay");
    EconomyReaverUnit->HangarCount = 4;
    HangarResources.Minerals = 15;
    Production.Update(14.0f);
    Require(EconomyReaverUnit->HangarCount == 4 &&
            EconomyReaverUnit->HangarBuildInProgress &&
            HangarResources.Minerals == 0,
        "Reaver preserves authored payload and reserves the 15 mineral scarab cost");
    Production.Update(1.0f);
    Require(EconomyReaverUnit->HangarCount == 5 &&
            !EconomyReaverUnit->HangarBuildInProgress,
        "Reaver completes exactly one scarab after 15 seconds");
    Production.Update(60.0f);
    Require(EconomyReaverUnit->HangarCount == 5 &&
            HangarResources.Minerals == 0,
        "base Reaver capacity stops automatic construction at five");

    HangarUpgrades.Levels[static_cast<size_t>(
        Game::Systems::ResearchId::Protoss_ReaverCapacity)] = 1;
    HangarResources.Minerals = 15;
    Production.Update(15.0f);
    Require(EconomyReaverUnit->HangarCount == 6 &&
            HangarResources.Minerals == 0,
        "Reaver Capacity permits another paid 15-second scarab build");
    EconomyReaverUnit->HangarCount = 10;

    const int HallucinatedCarrier = Game::Systems::ProductionSystem::DebugSpawnUnit(
        EM,
        Game::Races::GetUnitDefID("Carrier"),
        {288.0f, 128.0f},
        HangarEconomyPlayerID);
    auto* HallucinatedCarrierUnit =
        EM.GetComponent<Game::Components::UnitComponent>(HallucinatedCarrier);
    EM.AddComponent<Game::Components::StatusEffectComponent>(HallucinatedCarrier)
        .AddEffect(Game::Components::StatusEffect::Hallucination, 180.0f);
    HangarResources.Minerals = 100;
    Production.Update(100.0f);
    Require(HallucinatedCarrierUnit &&
            HallucinatedCarrierUnit->HangarCount == 0 &&
            !HallucinatedCarrierUnit->HangarBuildInProgress &&
            HangarResources.Minerals == 100,
        "hallucinated Carrier cannot spend minerals or construct real interceptors");

    // Test 5-item production queue limit
    {
        const int QTestPlayerID = 5;
        auto& QRes = Game::Systems::HarvestSystem::GetResources(QTestPlayerID);
        QRes.Minerals = 10000;
        QRes.VespeneGas = 10000;
        QRes.SupplyMax = 200;
        QRes.SupplyUsed = 0;
        const int QBuilding = CreateCompletedBuilding(EM, "Barracks", QTestPlayerID);
        const int TestMarineDefID = Game::Races::GetUnitDefID("Marine");
        for (int i = 0; i < 5; ++i)
        {
            Require(Game::Systems::ProductionSystem::CanTrain(EM, QBuilding, TestMarineDefID),
                "queue item within limit can be trained");
            Require(Game::Systems::ProductionSystem::StartTrain(EM, QBuilding, TestMarineDefID),
                "queue item added to building");
        }
        std::string QFailReason;
        Require(!Game::Systems::ProductionSystem::CanTrain(EM, QBuilding, TestMarineDefID, &QFailReason) &&
                QFailReason == "production queue is full",
            "production queue cannot exceed 5 items");
    }

    // Test 200 hard supply cap
    {
        const int SupplyPlayerID = 6;
        auto& SRes = Game::Systems::HarvestSystem::GetResources(SupplyPlayerID);
        SRes.Minerals = 5000;
        SRes.VespeneGas = 5000;
        SRes.SupplyMax = 300; // Accrued beyond 200
        SRes.SupplyUsed = 200; // Already at 200 cap
        const int SBuilding = CreateCompletedBuilding(EM, "Barracks", SupplyPlayerID);
        const int SupplyMarineDefID = Game::Races::GetUnitDefID("Marine");
        std::string SFailReason;
        Require(!Game::Systems::ProductionSystem::CanTrain(EM, SBuilding, SupplyMarineDefID, &SFailReason) &&
                SFailReason == "not enough supply",
            "training cannot exceed 200 supply even if SupplyMax > 200");
    }

    // Test Mutalisk morphs to Guardian and Devourer via Cocoon
    {
        Game::Systems::ZergSystem Zerg(SysMgr, nullptr);
        const int MorphPlayerID = 7;
        auto& MRes = Game::Systems::HarvestSystem::GetResources(MorphPlayerID);
        MRes.Minerals = 1000;
        MRes.VespeneGas = 1000;
        MRes.SupplyMax = 200;
        MRes.SupplyUsed = 10;

        const int MutaA = CreateCompletedUnit(EM, "Mutalisk", MorphPlayerID, {500.0f, 500.0f});
        const int MutaB = CreateCompletedUnit(EM, "Mutalisk", MorphPlayerID, {532.0f, 500.0f});

        std::string MorphReason;
        Require(!Game::Systems::ZergSystem::CanMorphMutaliskIntoGuardian(EM, MutaA, &MorphReason) &&
                MorphReason == "Greater Spire is required",
            "Guardian morph requires completed Greater Spire");
        Require(!Game::Systems::ZergSystem::CanMorphMutaliskIntoDevourer(EM, MutaB, &MorphReason) &&
                MorphReason == "Greater Spire is required",
            "Devourer morph requires completed Greater Spire");

        CreateCompletedBuilding(EM, "Greater Spire", MorphPlayerID);

        Require(Game::Systems::ZergSystem::CanMorphMutaliskIntoGuardian(EM, MutaA),
            "Guardian morph permitted with completed Greater Spire");
        Require(Game::Systems::ZergSystem::StartMorphMutaliskIntoGuardian(EM, MutaA),
            "Guardian morph successfully started");
        Require(!EM.IsAlive(MutaA), "source Mutalisk consumed for Guardian");

        // Verify Cocoon entity created
        int CocoonA = -1;
        for (int Eid : EM.GetEntitiesWith<Game::Components::UnitComponent>())
        {
            auto* U = EM.GetComponent<Game::Components::UnitComponent>(Eid);
            auto* P = EM.GetComponent<Game::Components::PlayerComponent>(Eid);
            if (U && P && P->PlayerID == MorphPlayerID && U->ChkUnitID == 59)
            {
                CocoonA = Eid;
                break;
            }
        }
        Require(CocoonA > 0, "Zerg Cocoon created for Guardian morph");
        auto* CocoonPath = EM.GetComponent<Game::Components::PathfindingComponent>(CocoonA);
        Require(CocoonPath && CocoonPath->IsAir, "Cocoon is classified as an air unit");

        Require(Game::Systems::ZergSystem::CanMorphMutaliskIntoDevourer(EM, MutaB),
            "Devourer morph permitted with completed Greater Spire");
        Require(Game::Systems::ZergSystem::StartMorphMutaliskIntoDevourer(EM, MutaB),
            "Devourer morph successfully started");
        Require(!EM.IsAlive(MutaB), "source Mutalisk consumed for Devourer");

        // Advance morphs until completion
        Zerg.Update(40.0f);
        Require(!EM.IsAlive(CocoonA), "Cocoon destroyed upon morph completion");

        bool FoundGuardian = false;
        bool FoundDevourer = false;
        for (int Eid : EM.GetEntitiesWith<Game::Components::UnitComponent>())
        {
            auto* U = EM.GetComponent<Game::Components::UnitComponent>(Eid);
            auto* P = EM.GetComponent<Game::Components::PlayerComponent>(Eid);
            if (U && P && P->PlayerID == MorphPlayerID)
            {
                if (U->ChkUnitID == 44) FoundGuardian = true;
                if (U->ChkUnitID == 62) FoundDevourer = true;
            }
        }
        Require(FoundGuardian, "Guardian spawned upon Cocoon completion");
        Require(FoundDevourer, "Devourer spawned upon Cocoon completion");
    }

    std::printf("ProductionEconomyTest passed\n");
    return 0;
}
