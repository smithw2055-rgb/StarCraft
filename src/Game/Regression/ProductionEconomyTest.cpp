#include "Game/Systems/ProductionSystem.h"

#include "Game/Map/Map.h"
#include "Game/Races/RaceDef.h"
#include "Game/Systems/CombatSystem.h"
#include "Game/Systems/CommandSystem.h"
#include "Game/Systems/DeathEvents.h"
#include "Game/Systems/HarvestSystem.h"
#include "Game/Systems/UpgradeSystem.h"
#include "Core/ECS/SystemManager.h"
#include "Game/Systems/ProtossSystem.h"
#include "Game/Systems/ZergSystem.h"

#include <cstdio>
#include <cstdlib>
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
    DoomedUnit->HitPoints = 0;
    const int MarinesBeforeDoomedDeath =
        CountUnitsByDef(EM, "Marine", DeadProductionPlayerID);
    Game::Systems::RecordUnitDeath(EM, DoomedBarracks, -1, false);
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
    EM.GetComponent<Game::Components::BuildingComponent>(Factory)->AddonID = MachineShop;
    const Core::Math::Vec2 RallyPoint = {520.0f, 432.0f};
    Game::Systems::CommandSystem::SetMap(nullptr);
    Game::Systems::CommandSystem::SetRallyPoint(EM, Factory, RallyPoint);

    const int GoliathDefID = Game::Races::GetUnitDefID("Goliath");
    const auto GoliathDef = Game::Races::GetUnitDefByID(GoliathDefID);

    Require(Game::Systems::ProductionSystem::StartTrain(EM, Factory, GoliathDefID),
        "Goliath can be queued from a completed factory with machine shop");
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
    Require(Game::Systems::ProductionSystem::StartTrain(EM, Hatchery, ZerglingDefID),
        "Zergling can be queued once Hatchery producer and Spawning Pool prerequisite are normalized");
    auto* HatcheryProd = EM.GetComponent<Game::Components::ProductionComponent>(Hatchery);
    Require(HatcheryProd && !HatcheryProd->Queue.empty() && HatcheryProd->Queue.back() == ZerglingDefID,
        "Hatchery production queue records the Zergling def id");
    Require(Resources.Minerals == 200 - ZerglingDef.MineralCost,
        "queued Zergling spends normalized Zerg unit costs");

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
    Require(ZerglingEgg > 0 && ZerglingEggRender &&
            ZerglingEggRender->ImageID == 50000u + 0x24u,
        "Zerg larva morph creates a visible Egg using the Egg image id");
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
    ZergMorphSystem.Update((float)LairDef.BuildTime * 0.5f);
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

    const int LairDroneLarva = CreateCompletedUnit(
        EM, "Larva", ZergBuildingMorphPlayerID, {912.0f, 552.0f});
    Require(Game::Systems::ZergSystem::StartMorphLarvaIntoUnit(
                EM, LairDroneLarva, Game::Races::GetUnitDefID("Drone")),
        "completed Lair satisfies the Hatchery prerequisite for larva production");

    CreateCompletedBuilding(
        EM, "Queen's Nest", ZergBuildingMorphPlayerID, {960.0f, 520.0f});
    const int HiveDefID = Game::Races::GetUnitDefID("Hive");
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

    ZergLarvaEconomyResources.Minerals = 500;
    ZergLarvaEconomyResources.VespeneGas = 500;
    ZergLarvaEconomyResources.SupplyMax = 30;
    const int LurkerLair = CreateCompletedBuilding(
        EM, "Lair", ZergLarvaEconomyPlayerID, {1120.0f, 520.0f});
    CreateCompletedBuilding(
        EM, "Hydralisk Den", ZergLarvaEconomyPlayerID, {1180.0f, 520.0f});
    const int LurkerLarva = CreateCompletedUnit(
        EM, "Larva", ZergLarvaEconomyPlayerID, {1120.0f, 520.0f});
    EM.AddComponent<Game::Components::LarvaComponent>(LurkerLarva)
        .ParentHatcheryEntityID = LurkerLair;
    const int LurkerDefID = Game::Races::GetUnitDefID("Lurker");
    const auto LurkerDef = Game::Races::GetUnitDefByID(LurkerDefID);
    Require(Game::Systems::ZergSystem::StartMorphLarvaIntoUnit(
                EM, LurkerLarva, LurkerDefID),
        "Lurker morph starts with its tech prerequisites available");
    ZergMorphSystem.Update((float)LurkerDef.BuildTime + 0.1f);
    const int MorphedLurker =
        FindUnitByDef(EM, "Lurker", ZergLarvaEconomyPlayerID);
    const auto* LurkerBurrow =
        EM.GetComponent<Game::Components::BurrowComponent>(MorphedLurker);
    const auto* LurkerTransform =
        EM.GetComponent<Game::Components::TransformComponent>(MorphedLurker);
    Require(LurkerBurrow && LurkerBurrow->CanBurrow &&
            !LurkerBurrow->IsBurrowed &&
            LurkerBurrow->Phase == Game::Components::BurrowPhase::Surface,
        "newly hatched Lurker explicitly starts on the surface and can move");
    Require(LurkerTransform && LurkerTransform->Position.Y > 520.0f,
        "newly hatched Lurker exits below its parent Lair");

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
        "Zerg Drone can start morphing into a Hatchery");
    Require(!EM.IsAlive(BuilderDrone), "Zerg Drone building morph consumes the source Drone");
    const int MorphingHatchery =
        FindMorphByTarget(EM, HatcheryDefID, ZergDroneBuildingPlayerID);
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
    ZergMorphSystem.Update(0.1f);
    Require(ZergMorphMap.GetTile(60, 61).HasCreep &&
            ZergMorphMap.GetTile(63, 63).HasCreep,
        "new Hatchery seeds continuous creep across its full 4x3 footprint");
    const int InitialConstructionCreep = CountCreepNearNewHatchery();
    ZergMorphSystem.Update(0.6f);
    Require(CountCreepNearNewHatchery() > InitialConstructionCreep,
        "Hatchery construction expands creep incrementally at the OpenBW spread interval");
    ZergMorphSystem.Update((float)HatcheryDef.BuildTime + 0.1f);
    Require(ZergDroneBuildingResources.SupplyMax ==
            10 + Game::Races::GetSupplyProvidedByDefID(HatcheryDefID),
        "completed Zerg Hatchery morph grants its supply cap");
    const int CompletedConstructionCreep = CountCreepNearNewHatchery();
    ZergMorphSystem.Update(2.0f);
    Require(CountCreepNearNewHatchery() == CompletedConstructionCreep,
        "completed Hatchery freezes its reached creep extent without further expansion");

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

    std::printf("ProductionEconomyTest passed\n");
    return 0;
}
