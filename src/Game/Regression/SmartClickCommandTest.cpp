#include "Main/GameCommandDispatchRuntime.h"

#include "Core/ECS/EntityManager.h"
#include "Game/Components.h"
#include "Game/Systems/CombatSystem.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace Game::Systems
{

void CombatSystem::IssueAttack(Core::ECS::EntityManager& EM,
                               const std::vector<int>& Attackers,
                               int TargetEntityID)
{
    for (int EntityID : Attackers)
    {
        auto* Command = EM.GetComponent<Game::Components::CommandComponent>(EntityID);
        if (!Command)
            Command = &EM.AddComponent<Game::Components::CommandComponent>(EntityID);
        Command->CurrentCommand = Game::Components::CommandType::Attack;
        Command->TargetEntityID = TargetEntityID;
    }
}

void CombatSystem::ApplyDamage(Core::ECS::EntityManager&,
                               int,
                               const Game::Components::CombatComponent&,
                               int,
                               int,
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

    int CreateUnit(Core::ECS::EntityManager& EM,
                   int ChkUnitID,
                   Core::Math::Vec2 Position,
                   int PlayerID)
    {
        const int EntityID = EM.CreateEntity();
        EM.AddComponent<Game::Components::TransformComponent>(EntityID).Position = Position;
        auto& Unit = EM.AddComponent<Game::Components::UnitComponent>(EntityID);
        Unit.ChkUnitID = ChkUnitID;
        Unit.HitPoints = 100;
        Unit.MaxHitPoints = 100;
        Unit.Speed = 3.0f;
        EM.AddComponent<Game::Components::PlayerComponent>(EntityID).PlayerID = PlayerID;
        return EntityID;
    }

    int CreateCompletedBuilding(Core::ECS::EntityManager& EM,
                                const char* DefName,
                                Core::Math::Vec2 Position,
                                int PlayerID)
    {
        const int DefID = Game::Races::GetUnitDefID(DefName);
        const auto Def = Game::Races::GetUnitDefByID(DefID);
        const int EntityID = CreateUnit(EM, Def.ChkUnitID, Position, PlayerID);
        auto* Unit = EM.GetComponent<Game::Components::UnitComponent>(EntityID);
        Unit->UnitRace = Def.RaceType;
        Unit->Speed = 0.0f;
        auto& Building = EM.AddComponent<Game::Components::BuildingComponent>(EntityID);
        Building.IsBuilding = true;
        Building.BuildingDefID = DefID;
        Building.UnderConstruction = false;
        return EntityID;
    }

    Game::Components::CombatComponent& AddCombat(
        Core::ECS::EntityManager& EM,
        int EntityID,
        int GroundDamage,
        int AirDamage,
        bool CanTargetGround,
        bool CanTargetAir,
        bool IsAir = false)
    {
        auto& Combat = EM.AddComponent<Game::Components::CombatComponent>(EntityID);
        Combat.GroundDamage = GroundDamage;
        Combat.AirDamage = AirDamage;
        Combat.CanTargetGround = CanTargetGround;
        Combat.CanTargetAir = CanTargetAir;
        Combat.AttackRange = 32.0f;
        Combat.MyElevation = IsAir ? Game::Components::Elevation::Air
                                   : Game::Components::Elevation::Ground;
        if (IsAir)
            EM.AddComponent<Game::Components::PathfindingComponent>(EntityID).IsAir = true;
        return Combat;
    }

    void TestMixedSelectionSmartClicksAirTarget()
    {
        Core::ECS::EntityManager EM;
        const int Marine = CreateUnit(EM, 0, {32.0f, 32.0f}, 0);
        AddCombat(EM, Marine, 6, 0, true, false);

        const int Goliath = CreateUnit(EM, 3, {48.0f, 32.0f}, 0);
        AddCombat(EM, Goliath, 12, 12, true, true);

        const int Mutalisk = CreateUnit(EM, 43, {160.0f, 96.0f}, 1);
        AddCombat(EM, Mutalisk, 9, 9, true, true, true);

        Game::Client::GameLoop::ClickTargetContext Target;
        Target.WorldPosition = {160.0f, 96.0f};
        Target.AnyEntityID = Mutalisk;
        Target.EnemyEntityID = Mutalisk;

        Game::Client::GameLoop::ExecuteSmartClickCommand(
            EM,
            {Marine, Goliath},
            Target,
            Target.WorldPosition);

        auto* MarineCmd = EM.GetComponent<Game::Components::CommandComponent>(Marine);
        Require(MarineCmd && MarineCmd->CurrentCommand == Game::Components::CommandType::Move,
            "smart-click moves units that cannot attack an air target");
        Require(MarineCmd->TargetPosition.X == Target.WorldPosition.X &&
                MarineCmd->TargetPosition.Y == Target.WorldPosition.Y,
            "smart-click movement keeps the clicked air target position");

        auto* GoliathCmd = EM.GetComponent<Game::Components::CommandComponent>(Goliath);
        Require(GoliathCmd && GoliathCmd->CurrentCommand == Game::Components::CommandType::Attack,
            "smart-click attacks with units that can attack an air target");
        Require(GoliathCmd->TargetEntityID == Mutalisk,
            "smart-click attack targets the clicked air unit");
    }

    void TestWorkerSmartClicksMinerals()
    {
        Core::ECS::EntityManager EM;
        const int Scv = CreateUnit(EM, 7, {64.0f, 64.0f}, 0);

        const int Marine = CreateUnit(EM, 0, {80.0f, 64.0f}, 0);
        AddCombat(EM, Marine, 6, 0, true, false);

        const int Mineral = CreateUnit(EM, 176, {160.0f, 64.0f}, 11);
        auto& Resource = EM.AddComponent<Game::Components::ResourceComponent>(Mineral);
        Resource.Type = Game::Components::ResourceType::Minerals;
        Resource.Amount = 1500;
        Resource.MaxAmount = 1500;

        Game::Client::GameLoop::ClickTargetContext Target;
        Target.WorldPosition = {160.0f, 64.0f};
        Target.AnyEntityID = Mineral;
        Target.ResourceEntityID = Mineral;

        Game::Client::GameLoop::ExecuteSmartClickCommand(
            EM,
            {Scv, Marine},
            Target,
            Target.WorldPosition);

        auto* ScvCmd = EM.GetComponent<Game::Components::CommandComponent>(Scv);
        auto* ScvHarvest = EM.GetComponent<Game::Components::HarvestComponent>(Scv);
        Require(ScvCmd && ScvCmd->CurrentCommand == Game::Components::CommandType::Harvest,
            "smart-clicking minerals issues harvest to workers");
        Require(ScvCmd->TargetEntityID == Mineral,
            "smart-click harvest keeps the clicked mineral target");
        Require(ScvHarvest && ScvHarvest->TargetResourceID == Mineral,
            "smart-click harvest creates worker harvest state");
        Require(!EM.GetComponent<Game::Components::HarvestComponent>(Marine),
            "smart-clicking minerals does not create harvest state for non-workers");
    }

    void TestSmartClickEmptyGroundMovesSelection()
    {
        Core::ECS::EntityManager EM;
        const int Scv = CreateUnit(EM, 7, {64.0f, 64.0f}, 0);
        const Core::Math::Vec2 TargetPos{192.0f, 128.0f};

        Game::Client::GameLoop::ClickTargetContext Target;
        Target.WorldPosition = TargetPos;

        Game::Client::GameLoop::ExecuteSmartClickCommand(
            EM,
            {Scv},
            Target,
            TargetPos);

        auto* Cmd = EM.GetComponent<Game::Components::CommandComponent>(Scv);
        auto* Path = EM.GetComponent<Game::Components::PathfindingComponent>(Scv);
        Require(Cmd && Cmd->CurrentCommand == Game::Components::CommandType::Move,
            "smart-clicking empty ground issues move");
        Require(Cmd->TargetPosition.X == TargetPos.X && Cmd->TargetPosition.Y == TargetPos.Y,
            "smart-click empty-ground move keeps clicked world position");
        Require(Path && !Path->Path.empty(),
            "smart-click empty-ground move creates a movement path");
    }

    void TestFactorySmartClickSetsRallyPoint()
    {
        Core::ECS::EntityManager EM;
        const int Barracks = CreateCompletedBuilding(EM, "Barracks", {96.0f, 96.0f}, 0);
        const int Marine = CreateUnit(EM, 0, {64.0f, 64.0f}, 0);
        AddCombat(EM, Marine, 6, 0, true, false);
        const Core::Math::Vec2 TargetPos{224.0f, 160.0f};

        Game::Client::GameLoop::ClickTargetContext Target;
        Target.WorldPosition = TargetPos;

        Game::Client::GameLoop::ExecuteSmartClickCommand(
            EM,
            {Barracks, Marine},
            Target,
            TargetPos);

        auto* BarracksCommand = EM.GetComponent<Game::Components::CommandComponent>(Barracks);
        Require(BarracksCommand && BarracksCommand->HasRallyPoint,
            "smart-clicking with a production building selected sets rally point");
        Require(BarracksCommand->RallyPoint.X == TargetPos.X &&
                BarracksCommand->RallyPoint.Y == TargetPos.Y,
            "production building rally point keeps clicked world position");

        auto* MarineCommand = EM.GetComponent<Game::Components::CommandComponent>(Marine);
        Require(MarineCommand && MarineCommand->CurrentCommand == Game::Components::CommandType::Move,
            "smart-click still moves non-factory units in a mixed selection");
    }

    void TestFriendlyUnitsSmartClickDropshipToBoard()
    {
        Core::ECS::EntityManager EM;
        const int Marine = CreateUnit(EM, 0, {64.0f, 64.0f}, 0);
        const int Firebat = CreateUnit(EM, 32, {80.0f, 64.0f}, 0);
        const int DistantMarine = CreateUnit(EM, 0, {0.0f, 0.0f}, 0);
        const int DistantFirebat = CreateUnit(EM, 32, {0.0f, 160.0f}, 0);
        const int Dropship = CreateUnit(EM, 11, {96.0f, 64.0f}, 0);
        EM.AddComponent<Game::Components::RenderComponent>(Marine).Visible = true;
        EM.AddComponent<Game::Components::RenderComponent>(Firebat).Visible = true;
        EM.AddComponent<Game::Components::RenderComponent>(DistantMarine).Visible = true;
        EM.AddComponent<Game::Components::RenderComponent>(DistantFirebat).Visible = true;

        Game::Client::GameLoop::ClickTargetContext Target;
        Target.WorldPosition = {96.0f, 64.0f};
        Target.AnyEntityID = Dropship;
        Game::Client::GameLoop::ExecuteSmartClickCommand(
            EM, {Marine, Firebat, DistantMarine, DistantFirebat}, Target, Target.WorldPosition);

        const auto* Loaded =
            EM.GetComponent<Game::Components::LoadedUnitsComponent>(Dropship);
        Require(Loaded && Loaded->PassengerIDs.size() == 2,
            "right-clicking a friendly Dropship boards selected ground units");
        Require(!EM.GetComponent<Game::Components::RenderComponent>(Marine)->Visible &&
                !EM.GetComponent<Game::Components::RenderComponent>(Firebat)->Visible,
            "right-click boarding hides every loaded passenger");
        const auto* Pending =
            EM.GetComponent<Game::Components::PendingBoardTransportComponent>(DistantMarine);
        auto* DistantCommand =
            EM.GetComponent<Game::Components::CommandComponent>(DistantMarine);
        const auto* TransportPlan =
            EM.GetComponent<Game::Components::PendingTransportBoardingComponent>(Dropship);
        const auto* DropshipCommand =
            EM.GetComponent<Game::Components::CommandComponent>(Dropship);
        Require(Pending && Pending->TransportID == Dropship && DistantCommand &&
                DistantCommand->CurrentCommand == Game::Components::CommandType::Move,
            "distant selected unit automatically approaches the right-clicked Dropship");
        Require(TransportPlan && TransportPlan->PassengerIDs.size() == 2 &&
                TransportPlan->PassengerIDs.front() == DistantMarine && DropshipCommand &&
                DropshipCommand->CurrentCommand == Game::Components::CommandType::Move &&
                DropshipCommand->TargetPosition.X == 0.0f &&
                DropshipCommand->TargetPosition.Y == 0.0f,
            "Dropship flies toward a distant passenger instead of waiting in place");

        DistantCommand->CurrentCommand = Game::Components::CommandType::Stop;
        if (auto* Path = EM.GetComponent<Game::Components::PathfindingComponent>(DistantMarine))
            Path->Path.clear();
        EM.GetComponent<Game::Components::TransformComponent>(Dropship)->Position = {8.0f, 0.0f};
        Game::Systems::TransportSystem::UpdateOrders(EM, 0.0f);
        Require(Loaded->PassengerIDs.size() == 3 &&
                !EM.GetComponent<Game::Components::RenderComponent>(DistantMarine)->Visible,
            "Dropship collects a passenger even when the ground unit cannot approach it");

        Game::Systems::TransportSystem::UpdateOrders(EM, 0.0f);
        Require(DropshipCommand->CurrentCommand == Game::Components::CommandType::Move &&
                DropshipCommand->TargetPosition.X == 0.0f &&
                DropshipCommand->TargetPosition.Y == 160.0f,
            "Dropship continues to the next passenger in the boarding queue");
        EM.GetComponent<Game::Components::TransformComponent>(Dropship)->Position = {8.0f, 160.0f};
        Game::Systems::TransportSystem::UpdateOrders(EM, 0.0f);
        Require(Loaded->PassengerIDs.size() == 4 &&
                !EM.GetComponent<Game::Components::RenderComponent>(DistantFirebat)->Visible,
            "Dropship boards each queued passenger in sequence");
    }

    void TestScvsSmartClickIncompleteCommandCenterToAssist()
    {
        Core::ECS::EntityManager EM;
        const int CommandCenter = CreateCompletedBuilding(
            EM, "CommandCenter", {160.0f, 96.0f}, 0);
        auto* Building = EM.GetComponent<Game::Components::BuildingComponent>(CommandCenter);
        Require(Building != nullptr, "assist target has building state");
        Building->UnderConstruction = true;

        const int PrimaryScv = CreateUnit(EM, 7, {96.0f, 96.0f}, 0);
        const int AssistantA = CreateUnit(EM, 7, {64.0f, 64.0f}, 0);
        const int AssistantB = CreateUnit(EM, 7, {64.0f, 128.0f}, 0);
        Building->BuilderUnitID = PrimaryScv;

        Game::Client::GameLoop::ClickTargetContext Target;
        Target.WorldPosition = {160.0f, 96.0f};
        Target.AnyEntityID = CommandCenter;
        Game::Client::GameLoop::ExecuteSmartClickCommand(
            EM, {AssistantA, AssistantB}, Target, Target.WorldPosition);

        Require(Building->AssistantBuilderUnitIDs.size() == 2,
            "right-clicking an unfinished Command Center assigns every selected SCV");
        for (int AssistantID : {AssistantA, AssistantB})
        {
            const auto* Path =
                EM.GetComponent<Game::Components::PathfindingComponent>(AssistantID);
            Require(Path && Path->RequestedIgnoreEntityID == CommandCenter,
                "assisting SCV approaches the construction footprint");
        }
    }

    void TestScvSmartClicksDamagedFriendlyBuildingToRepair()
    {
        Core::ECS::EntityManager EM;
        const int Barracks = CreateCompletedBuilding(
            EM, "Barracks", {160.0f, 96.0f}, 0);
        auto* BarracksUnit = EM.GetComponent<Game::Components::UnitComponent>(Barracks);
        Require(BarracksUnit != nullptr, "repair target has unit state");
        BarracksUnit->HitPoints = 40;
        BarracksUnit->MaxHitPoints = 100;

        const int Scv = CreateUnit(EM, 7, {64.0f, 96.0f}, 0);
        Game::Client::GameLoop::ClickTargetContext Target;
        Target.WorldPosition = {160.0f, 96.0f};
        Target.AnyEntityID = Barracks;
        Game::Client::GameLoop::ExecuteSmartClickCommand(
            EM, {Scv}, Target, Target.WorldPosition);

        const auto* Command =
            EM.GetComponent<Game::Components::CommandComponent>(Scv);
        const auto* Path =
            EM.GetComponent<Game::Components::PathfindingComponent>(Scv);
        Require(Command &&
                Command->CurrentCommand == Game::Components::CommandType::Repair &&
                Command->TargetEntityID == Barracks,
            "right-clicking a damaged friendly Terran building issues Repair");
        Require(Path && Path->RequestedIgnoreEntityID == Barracks,
            "repairing SCV approaches the building collision footprint");
    }

    void TestScvSmartClicksDamagedFriendlyMechanicalUnitToRepair()
    {
        Core::ECS::EntityManager EM;
        // ChkUnitID 5 is Siege Tank (Tank Mode)
        const int Tank = CreateUnit(EM, 5, {160.0f, 96.0f}, 0);
        auto* TankUnit = EM.GetComponent<Game::Components::UnitComponent>(Tank);
        Require(TankUnit != nullptr, "tank has unit state");
        TankUnit->HitPoints = 50;
        TankUnit->MaxHitPoints = 150;

        const int Scv = CreateUnit(EM, 7, {64.0f, 96.0f}, 0);
        Game::Client::GameLoop::ClickTargetContext Target;
        Target.WorldPosition = {160.0f, 96.0f};
        Target.AnyEntityID = Tank;
        Game::Client::GameLoop::ExecuteSmartClickCommand(
            EM, {Scv}, Target, Target.WorldPosition);

        const auto* Command =
            EM.GetComponent<Game::Components::CommandComponent>(Scv);
        Require(Command &&
                Command->CurrentCommand == Game::Components::CommandType::Repair &&
                Command->TargetEntityID == Tank,
            "right-clicking a damaged friendly Terran mechanical unit issues Repair");
    }

    void TestMedicSmartClicksDamagedFriendlyOrganicUnitToHeal()
    {
        Core::ECS::EntityManager EM;
        // ChkUnitID 0 is Marine
        const int Marine = CreateUnit(EM, 0, {160.0f, 96.0f}, 0);
        auto* MarineUnit = EM.GetComponent<Game::Components::UnitComponent>(Marine);
        Require(MarineUnit != nullptr, "marine has unit state");
        MarineUnit->HitPoints = 20;
        MarineUnit->MaxHitPoints = 40;

        // ChkUnitID 34 is Medic
        const int Medic = CreateUnit(EM, 34, {64.0f, 96.0f}, 0);
        auto* MedicUnit = EM.GetComponent<Game::Components::UnitComponent>(Medic);
        Require(MedicUnit != nullptr, "medic has unit state");
        MedicUnit->EnergyPoints = 50;

        Game::Client::GameLoop::ClickTargetContext Target;
        Target.WorldPosition = {160.0f, 96.0f};
        Target.AnyEntityID = Marine;
        Game::Client::GameLoop::ExecuteSmartClickCommand(
            EM, {Medic}, Target, Target.WorldPosition);

        const auto* Command =
            EM.GetComponent<Game::Components::CommandComponent>(Medic);
        Require(Command &&
                Command->CurrentCommand == Game::Components::CommandType::Heal &&
                Command->TargetEntityID == Marine,
            "right-clicking a damaged friendly organic unit issues Heal to Medic");
    }

    void TestFriendlyProtossUnitSmartClicksShieldBatteryToRecharge()
    {
        Core::ECS::EntityManager EM;
        // ChkUnitID 172 is Shield Battery
        const int Battery = CreateCompletedBuilding(EM, "Shield Battery", {160.0f, 96.0f}, 0);
        auto* BatteryUnit = EM.GetComponent<Game::Components::UnitComponent>(Battery);
        Require(BatteryUnit != nullptr, "battery has unit state");
        BatteryUnit->ChkUnitID = 172;
        BatteryUnit->EnergyPoints = 100;
        BatteryUnit->MaxEnergyPoints = 200;

        // ChkUnitID 65 is Zealot
        const int Zealot = CreateUnit(EM, 65, {128.0f, 96.0f}, 0);
        auto* ZealotUnit = EM.GetComponent<Game::Components::UnitComponent>(Zealot);
        Require(ZealotUnit != nullptr, "zealot has unit state");
        ZealotUnit->UnitRace = Game::Components::Race::Protoss;
        ZealotUnit->ShieldPoints = 10;
        ZealotUnit->MaxShieldPoints = 60;

        Game::Client::GameLoop::ClickTargetContext Target;
        Target.WorldPosition = {160.0f, 96.0f};
        Target.AnyEntityID = Battery;
        Game::Client::GameLoop::ExecuteSmartClickCommand(
            EM, {Zealot}, Target, Target.WorldPosition);

        Require(ZealotUnit->ShieldPoints == 60,
            "right-clicking nearby friendly Shield Battery restores full shields");
        Require(BatteryUnit->EnergyPoints < 100,
            "Shield Battery consumed energy restoring zealot shields");
    }
}

int main()
{
    TestMixedSelectionSmartClicksAirTarget();
    TestWorkerSmartClicksMinerals();
    TestSmartClickEmptyGroundMovesSelection();
    TestFactorySmartClickSetsRallyPoint();
    TestFriendlyUnitsSmartClickDropshipToBoard();
    TestScvsSmartClickIncompleteCommandCenterToAssist();
    TestScvSmartClicksDamagedFriendlyBuildingToRepair();
    TestScvSmartClicksDamagedFriendlyMechanicalUnitToRepair();
    TestMedicSmartClicksDamagedFriendlyOrganicUnitToHeal();
    TestFriendlyProtossUnitSmartClicksShieldBatteryToRecharge();

    std::printf("SmartClickCommandTest passed\n");
    return 0;
}
