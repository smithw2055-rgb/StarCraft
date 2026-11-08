#include "Game/Systems/CommandSystem.h"

#include "Core/ECS/SystemManager.h"
#include "Game/DataEnums.h"
#include "Game/Map/Map.h"
#include "Game/Systems/CombatSystem.h"
#include "Game/Systems/BurrowState.h"
#include "Game/Systems/HarvestSystem.h"
#include "Main/GameCommandDispatchRuntime.h"
#include "Game/Systems/MovementSystem.h"
#include "Game/Systems/UpgradeSystem.h"
#include "Game/Races/RaceDef.h"

#include <cstdio>
#include <cstdlib>
#include <algorithm>
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

    bool NearlyEqual(float A, float B)
    {
        return std::fabs(A - B) < 0.01f;
    }

    int CreateUnit(Core::ECS::EntityManager& EM,
                   int ChkUnitID,
                   float Speed,
                   Core::Math::Vec2 Pos)
    {
        int EntityID = EM.CreateEntity();
        EM.AddComponent<Game::Components::TransformComponent>(EntityID).Position = Pos;
        auto& Unit = EM.AddComponent<Game::Components::UnitComponent>(EntityID);
        Unit.ChkUnitID = ChkUnitID;
        Unit.Speed = Speed;
        return EntityID;
    }

    void SetCollisionBounds(Game::Components::PathfindingComponent& Path,
                            int Left,
                            int Up,
                            int Right,
                            int Down)
    {
        Path.HasCollisionBounds = true;
        Path.CollisionLeft = Left;
        Path.CollisionUp = Up;
        Path.CollisionRight = Right;
        Path.CollisionDown = Down;
        Path.CollisionRadius =
            std::max(4, std::max(Left + Right + 1, Up + Down + 1) / 2);
    }

    bool CollisionBoundsOverlap(
        const Game::Components::TransformComponent& ATrans,
        const Game::Components::PathfindingComponent& APath,
        const Game::Components::TransformComponent& BTrans,
        const Game::Components::PathfindingComponent& BPath)
    {
        return ATrans.Position.X - (float)APath.CollisionLeft <
                   BTrans.Position.X + (float)BPath.CollisionRight + 1.0f &&
               ATrans.Position.X + (float)APath.CollisionRight + 1.0f >
                   BTrans.Position.X - (float)BPath.CollisionLeft &&
               ATrans.Position.Y - (float)APath.CollisionUp <
                   BTrans.Position.Y + (float)BPath.CollisionDown + 1.0f &&
               ATrans.Position.Y + (float)APath.CollisionDown + 1.0f >
                   BTrans.Position.Y - (float)BPath.CollisionUp;
    }
}

int main()
{
    Core::ECS::SystemManager SysMgr;
    auto& EM = SysMgr.GetEntityManager();
    Game::Systems::CommandSystem::SetMap(nullptr);

    int CommandCenter = CreateUnit(
        EM,
        106,
        0.0f,
        {64.0f, 64.0f});
    auto& CcBuilding = EM.AddComponent<Game::Components::BuildingComponent>(CommandCenter);
    CcBuilding.IsBuilding = true;
    CcBuilding.IsLifted = false;
    auto& CcPath = EM.AddComponent<Game::Components::PathfindingComponent>(CommandCenter);
    CcPath.Path.push_back({96.0f, 64.0f});

    Game::Systems::CommandSystem::IssueMove(EM, {CommandCenter}, {192.0f, 64.0f});

    Require(CcPath.Path.empty(),
        "landed buildings reject move orders and clear stale paths");
    Require(!EM.GetComponent<Game::Components::CommandComponent>(CommandCenter),
        "landed buildings do not receive a move command component");

    int FlightBuilding = CreateUnit(EM, 111, 0.0f, {64.0f, 160.0f});
    auto& FlightBuildingState =
        EM.AddComponent<Game::Components::BuildingComponent>(FlightBuilding);
    FlightBuildingState.IsBuilding = true;
    FlightBuildingState.IsLifted = true;
    FlightBuildingState.FlightPhase =
        Game::Components::BuildingFlightPhase::LiftingOff;
    auto& FlightPath =
        EM.AddComponent<Game::Components::PathfindingComponent>(FlightBuilding);
    Game::Systems::CommandSystem::IssueMove(EM, {FlightBuilding}, {192.0f, 160.0f});
    Require(FlightPath.Path.empty() &&
            !EM.GetComponent<Game::Components::CommandComponent>(FlightBuilding),
        "building rejects movement while the LiftOff script transition is active");
    FlightBuildingState.FlightPhase =
        Game::Components::BuildingFlightPhase::Airborne;
    EM.GetComponent<Game::Components::UnitComponent>(FlightBuilding)->Speed = 2.0f;
    Game::Systems::CommandSystem::IssueMove(EM, {FlightBuilding}, {192.0f, 160.0f});
    Require(!FlightPath.Path.empty() &&
            EM.GetComponent<Game::Components::CommandComponent>(FlightBuilding),
        "building accepts movement after LiftOff animation completion");

    FlightPath.StallTimer = 1.0f;
    FlightPath.HasLastWaypointDistance = true;
    FlightPath.DestinationStallTimer = 9.0f;
    FlightPath.HasLastDestinationDistance = true;
    FlightPath.RepathFailCount = 2;
    Game::Systems::CommandSystem::IssueMove(
        EM, {FlightBuilding}, {224.0f, 160.0f});
    Require(FlightPath.StallTimer == 0.0f &&
            !FlightPath.HasLastWaypointDistance &&
            FlightPath.DestinationStallTimer == 0.0f &&
            !FlightPath.HasLastDestinationDistance &&
            FlightPath.RepathFailCount == 0,
        "a new movement command clears all stall and reroute history");

    Game::Components::Order AttackMoveOrder;
    AttackMoveOrder.Type = Game::Components::OrderType::AttackMove;
    AttackMoveOrder.TargetPosition = {192.0f, 96.0f};
    CcPath.Path.push_back({96.0f, 64.0f});
    Game::Systems::CommandSystem::IssueOrder(EM, CommandCenter, AttackMoveOrder, false);
    Require(CcPath.Path.empty(),
        "landed buildings reject attack-move orders and clear stale paths");
    Require(!EM.GetComponent<Game::Components::CommandComponent>(CommandCenter),
        "landed buildings do not receive an attack-move command component");

    Game::Components::Order PatrolOrder;
    PatrolOrder.Type = Game::Components::OrderType::Patrol;
    PatrolOrder.TargetPosition = {192.0f, 128.0f};
    CcPath.Path.push_back({96.0f, 64.0f});
    Game::Systems::CommandSystem::IssueOrder(EM, CommandCenter, PatrolOrder, false);
    Require(CcPath.Path.empty(),
        "landed buildings reject patrol orders and clear stale paths");
    Require(!EM.GetComponent<Game::Components::CommandComponent>(CommandCenter),
        "landed buildings do not receive a patrol command component");

    Game::Components::Order FollowOrder;
    FollowOrder.Type = Game::Components::OrderType::Follow;
    FollowOrder.TargetPosition = {192.0f, 160.0f};
    FollowOrder.TargetEntityID = 1234;
    CcPath.Path.push_back({96.0f, 64.0f});
    Game::Systems::CommandSystem::IssueOrder(EM, CommandCenter, FollowOrder, false);
    Require(CcPath.Path.empty(),
        "landed buildings reject follow orders and clear stale paths");
    Require(!EM.GetComponent<Game::Components::CommandComponent>(CommandCenter),
        "landed buildings do not receive a follow command component");

    int SiegedTank = CreateUnit(
        EM,
        30,
        2.3f,
        {64.0f, 128.0f});
    auto& TankPath = EM.AddComponent<Game::Components::PathfindingComponent>(SiegedTank);
    TankPath.Path.push_back({96.0f, 128.0f});

    Game::Systems::CommandSystem::IssueMove(EM, {SiegedTank}, {192.0f, 128.0f});

    Require(TankPath.Path.empty(),
        "sieged tanks reject move orders and clear stale paths");
    Require(!EM.GetComponent<Game::Components::CommandComponent>(SiegedTank),
        "sieged tanks do not receive a move command component");

    constexpr int SiegeUpgradePlayerID = 17;
    auto& SiegeUpgrades =
        Game::Systems::UpgradeSystem::GetUpgrades(SiegeUpgradePlayerID);
    SiegeUpgrades.Levels.fill(0);
    SiegeUpgrades.Levels[static_cast<size_t>(
        Game::Systems::ResearchId::Terran_SiegeMode)] = 1;
    const int MobileTank = CreateUnit(EM, 5, 2.3f, {96.0f, 128.0f});
    EM.AddComponent<Game::Components::PlayerComponent>(MobileTank).PlayerID =
        SiegeUpgradePlayerID;
    auto& MobileTankCombat =
        EM.AddComponent<Game::Components::CombatComponent>(MobileTank);
    Game::Races::ApplyCombatDefinition(
        MobileTankCombat, Game::Races::GetUnitDefByName("SiegeTank"), false);
    Require(Game::Systems::CommandSystem::SetSiegeMode(EM, MobileTank, true),
        "researched tank begins its siege transition");
    const auto* SiegeTransition =
        EM.GetComponent<Game::Components::SiegeTransitionComponent>(MobileTank);
    Require(SiegeTransition &&
            SiegeTransition->TargetCombat.GroundDamage == 70 &&
            SiegeTransition->TargetCombat.GroundDamageBonus == 5 &&
            SiegeTransition->TargetCombat.MinimumAttackRange == 2.0f &&
            SiegeTransition->TargetCombat.GroundAttackRange == 12.0f,
        "siege transition carries classic damage, upgrade step, range, and dead zone");

    int Lurker = CreateUnit(
        EM,
        103,
        2.5f,
        {64.0f, 192.0f});
    auto& LurkerPath =
        EM.AddComponent<Game::Components::PathfindingComponent>(Lurker);
    auto& LurkerBurrow =
        EM.AddComponent<Game::Components::BurrowComponent>(Lurker);
    LurkerBurrow.CanBurrow = true;
    Game::Systems::CommandSystem::IssueMove(
        EM, {Lurker}, {192.0f, 192.0f});
    auto* LurkerCommand =
        EM.GetComponent<Game::Components::CommandComponent>(Lurker);
    Require(LurkerCommand &&
            LurkerCommand->CurrentCommand == Game::Components::CommandType::Move &&
            !LurkerPath.Path.empty(),
        "surface Lurker receives a move command and movement path");

    int Scv = CreateUnit(
        EM,
        7,
        2.8f,
        {32.0f, 32.0f});

    int Mineral = EM.CreateEntity();
    auto& MineralResource =
        EM.AddComponent<Game::Components::ResourceComponent>(Mineral);
    MineralResource.Type = Game::Components::ResourceType::Minerals;
    MineralResource.Amount = 1500;
    const int OtherScv = CreateUnit(EM, 7, 2.8f, {32.0f, 64.0f});
    MineralResource.ActiveGathererEntityID = Scv;
    MineralResource.QueuedGatherers = {Scv, OtherScv};
    auto& ScvHarvest = EM.AddComponent<Game::Components::HarvestComponent>(Scv);
    ScvHarvest.State = Game::Components::HarvestState::Mining;
    ScvHarvest.TargetResourceID = Mineral;
    ScvHarvest.DesiredResourceID = Mineral;
    ScvHarvest.QueuedAtResource = true;
    ScvHarvest.InsideGasBuilding = true;
    ScvHarvest.GasExitPosition = {48.0f, 32.0f};
    auto& ScvRender = EM.AddComponent<Game::Components::RenderComponent>(Scv);
    ScvRender.Visible = false;

    Game::Systems::CommandSystem::IssueMove(EM, {Scv}, {160.0f, 32.0f});

    auto* ScvCmd = EM.GetComponent<Game::Components::CommandComponent>(Scv);
    auto* ScvPath = EM.GetComponent<Game::Components::PathfindingComponent>(Scv);
    Require(ScvCmd && ScvCmd->CurrentCommand == Game::Components::CommandType::Move,
        "mobile units receive move commands");
    Require(ScvPath && !ScvPath->Path.empty(),
        "mobile units receive movement paths");
    Require(ScvHarvest.State == Game::Components::HarvestState::Idle &&
            ScvHarvest.TargetResourceID < 0 &&
            MineralResource.ActiveGathererEntityID < 0 &&
            MineralResource.QueuedGatherers.size() == 1 &&
            MineralResource.QueuedGatherers.front() == OtherScv &&
            !ScvHarvest.InsideGasBuilding && ScvRender.Visible &&
            EM.GetComponent<Game::Components::TransformComponent>(Scv)->Position
                    .DistanceTo({48.0f, 32.0f}) < 0.1f,
        "a player move releases only the commanded SCV and restores it from gas");

    ScvHarvest.State = Game::Components::HarvestState::MovingToResource;
    ScvHarvest.TargetResourceID = Mineral;
    ScvHarvest.DesiredResourceID = Mineral;
    MineralResource.QueuedGatherers = {Scv};
    Require(Game::Systems::CommandSystem::IssueMoveAndHasPath(
                EM, Scv, {192.0f, 32.0f}, Mineral),
        "an internal harvest approach move receives a path");
    Require(ScvHarvest.State == Game::Components::HarvestState::MovingToResource &&
            ScvHarvest.TargetResourceID == Mineral &&
            MineralResource.QueuedGatherers.size() == 1,
        "an internal harvest approach move preserves the worker assignment");

    int GroupMarineA = CreateUnit(EM, 0, 4.0f, {32.0f, 32.0f});
    int GroupMarineB = CreateUnit(EM, 0, 4.0f, {64.0f, 32.0f});
    int GroupMarineC = CreateUnit(EM, 0, 4.0f, {32.0f, 64.0f});
    const std::vector<int> MoveGroup{GroupMarineA, GroupMarineB, GroupMarineC};
    Game::Systems::CommandSystem::IssueMove(EM, MoveGroup, {320.0f, 320.0f});

    auto* GroupCmdA = EM.GetComponent<Game::Components::CommandComponent>(GroupMarineA);
    auto* GroupCmdB = EM.GetComponent<Game::Components::CommandComponent>(GroupMarineB);
    auto* GroupCmdC = EM.GetComponent<Game::Components::CommandComponent>(GroupMarineC);
    Require(GroupCmdA && GroupCmdB && GroupCmdC,
        "group move assigns commands to every mobile selected unit");
    Require(NearlyEqual(GroupCmdB->TargetPosition.X - GroupCmdA->TargetPosition.X, 25.6f) &&
            NearlyEqual(GroupCmdC->TargetPosition.Y - GroupCmdA->TargetPosition.Y, 25.6f),
        "compact infantry group closes ranks without collapsing onto one point");
    Require(NearlyEqual((GroupCmdA->TargetPosition.X + GroupCmdB->TargetPosition.X +
                         GroupCmdC->TargetPosition.X) / 3.0f, 320.0f) &&
            NearlyEqual((GroupCmdA->TargetPosition.Y + GroupCmdB->TargetPosition.Y +
                         GroupCmdC->TargetPosition.Y) / 3.0f, 320.0f),
        "compact group move centers the formation on the clicked destination");

    Game::Systems::CommandSystem::IssueMove(EM, MoveGroup, {48.0f, 48.0f});
    Require(NearlyEqual(GroupCmdA->TargetPosition.X, 32.0f) &&
            NearlyEqual(GroupCmdA->TargetPosition.Y, 32.0f) &&
            NearlyEqual(GroupCmdB->TargetPosition.X, 64.0f) &&
            NearlyEqual(GroupCmdC->TargetPosition.Y, 64.0f),
        "clicking inside a compact group does not collapse it onto one point");

    Game::Components::Order QueuedGroupMove;
    QueuedGroupMove.Type = Game::Components::OrderType::Move;
    QueuedGroupMove.TargetPosition = {480.0f, 320.0f};
    Game::Systems::CommandSystem::IssueOrder(EM, MoveGroup, QueuedGroupMove, true);
    Require(GroupCmdA->Orders.size() == 2 &&
            GroupCmdB->Orders.size() == 2 &&
            GroupCmdC->Orders.size() == 2,
        "queued group move appends one order per selected unit");
    Require(NearlyEqual(GroupCmdB->Orders[1].TargetPosition.X -
                        GroupCmdA->Orders[1].TargetPosition.X, 25.6f) &&
            NearlyEqual(GroupCmdC->Orders[1].TargetPosition.Y -
                        GroupCmdA->Orders[1].TargetPosition.Y, 25.6f),
        "queued group move stores per-unit formation positions");

    Game::Systems::CommandSystem::IssueAttackMove(EM, MoveGroup, {640.0f, 320.0f});
    Require(GroupCmdA->CurrentCommand == Game::Components::CommandType::AttackMove &&
            GroupCmdB->CurrentCommand == Game::Components::CommandType::AttackMove &&
            NearlyEqual(GroupCmdB->TargetPosition.X - GroupCmdA->TargetPosition.X, 25.6f),
        "attack-move uses the same per-unit group positioning semantics");

    int WideMarineA = CreateUnit(EM, 0, 4.0f, {0.0f, 160.0f});
    int WideMarineB = CreateUnit(EM, 0, 4.0f, {256.0f, 160.0f});
    Game::Systems::CommandSystem::IssueMove(
        EM, {WideMarineA, WideMarineB}, {400.0f, 160.0f});
    auto* WideCmdA = EM.GetComponent<Game::Components::CommandComponent>(WideMarineA);
    auto* WideCmdB = EM.GetComponent<Game::Components::CommandComponent>(WideMarineB);
    Require(WideCmdA && WideCmdB &&
            WideCmdA->TargetPosition.DistanceTo(WideCmdB->TargetPosition) >= 23.9f &&
            NearlyEqual((WideCmdA->TargetPosition.X + WideCmdB->TargetPosition.X) * 0.5f,
                        400.0f),
        "widely scattered selections converge into distinct centered formation slots");

    const std::vector<int> DispersedGroup{
        CreateUnit(EM, 0, 4.0f, {0.0f, 0.0f}),
        CreateUnit(EM, 0, 4.0f, {800.0f, 0.0f}),
        CreateUnit(EM, 0, 4.0f, {0.0f, 800.0f}),
        CreateUnit(EM, 0, 4.0f, {800.0f, 800.0f})
    };
    Game::Systems::CommandSystem::IssueAttackMove(
        EM, DispersedGroup, {400.0f, 400.0f});
    Core::Math::Vec2 DispersedTargetCenter;
    for (size_t I = 0; I < DispersedGroup.size(); ++I)
    {
        const auto* CommandI =
            EM.GetComponent<Game::Components::CommandComponent>(DispersedGroup[I]);
        Require(CommandI, "dispersed attack group receives commands");
        DispersedTargetCenter += CommandI->TargetPosition;
        for (size_t J = 0; J < I; ++J)
        {
            const auto* CommandJ =
                EM.GetComponent<Game::Components::CommandComponent>(DispersedGroup[J]);
            Require(CommandI->TargetPosition.DistanceTo(CommandJ->TargetPosition) >= 47.9f,
                "dispersed group target inside its broad bounds still gets unique slots");
        }
    }
    DispersedTargetCenter =
        DispersedTargetCenter / (float)DispersedGroup.size();
    Require(NearlyEqual(DispersedTargetCenter.X, 400.0f) &&
            NearlyEqual(DispersedTargetCenter.Y, 400.0f),
        "dispersed group formation remains centered on its interior destination");

    std::vector<int> MassiveAttackGroup;
    for (int I = 0; I < 64; ++I)
    {
        MassiveAttackGroup.push_back(CreateUnit(
            EM, 0, 4.0f,
            {(float)((I % 8) * 128), (float)((I / 8) * 128)}));
    }
    Game::Systems::CommandSystem::IssueAttackMove(
        EM, MassiveAttackGroup, {448.0f, 448.0f});
    for (int ID : MassiveAttackGroup)
    {
        const auto* MassivePath =
            EM.GetComponent<Game::Components::PathfindingComponent>(ID);
        Require(MassivePath && MassivePath->NodeTolerance >= 127.9f,
            "massive attack formations accept a stable crowd arrival envelope");
    }

    const int SpiderMine = CreateUnit(EM, 13, 10.0f, {96.0f, 224.0f});
    auto& SpiderMinePath =
        EM.AddComponent<Game::Components::PathfindingComponent>(SpiderMine);
    SpiderMinePath.Path.push_back({128.0f, 224.0f});
    EM.AddComponent<Game::Components::SpiderMineComponent>(SpiderMine).IsPlaced = true;
    Game::Systems::CommandSystem::IssueMove(EM, {SpiderMine}, {320.0f, 224.0f});
    Require(SpiderMinePath.Path.empty() &&
            !EM.GetComponent<Game::Components::CommandComponent>(SpiderMine),
        "placed Spider Mine rejects player movement orders");

    const int BurrowedZergling = CreateUnit(EM, 37, 3.4f, {144.0f, 224.0f});
    auto& BurrowedPath =
        EM.AddComponent<Game::Components::PathfindingComponent>(BurrowedZergling);
    BurrowedPath.Path.push_back({176.0f, 224.0f});
    auto& BurrowedState =
        EM.AddComponent<Game::Components::BurrowComponent>(BurrowedZergling);
    BurrowedState.CanBurrow = true;
    BurrowedState.IsBurrowed = true;
    BurrowedState.Phase = Game::Components::BurrowPhase::Burrowed;
    Game::Systems::CommandSystem::IssueMove(
        EM, {BurrowedZergling}, {320.0f, 224.0f});
    Require(BurrowedPath.Path.empty() &&
            !EM.GetComponent<Game::Components::CommandComponent>(BurrowedZergling),
        "burrowed units reject movement until an UnBurrow transition completes");

    const int BurrowingHydralisk = CreateUnit(EM, 38, 2.5f, {192.0f, 224.0f});
    auto& BurrowingPath =
        EM.AddComponent<Game::Components::PathfindingComponent>(BurrowingHydralisk);
    BurrowingPath.Path.push_back({224.0f, 224.0f});
    auto& BurrowingState =
        EM.AddComponent<Game::Components::BurrowComponent>(BurrowingHydralisk);
    BurrowingState.CanBurrow = true;
    BurrowingState.Phase = Game::Components::BurrowPhase::Burrowing;
    Game::Systems::CommandSystem::IssueMove(
        EM, {BurrowingHydralisk}, {320.0f, 224.0f});
    Require(BurrowingPath.Path.empty() &&
            !EM.GetComponent<Game::Components::CommandComponent>(BurrowingHydralisk),
        "units cannot move while the Burrow iscript transition is active");

    Core::ECS::SystemManager TriggerBurrowSysMgr;
    auto& TriggerBurrowEM = TriggerBurrowSysMgr.GetEntityManager();
    Game::Map::GameMap TriggerBurrowMap;
    TriggerBurrowMap.Generate(16, 16);
    for (int Y = 0; Y < TriggerBurrowMap.GetHeight(); ++Y)
    {
        for (int X = 0; X < TriggerBurrowMap.GetWidth(); ++X)
        {
            auto Tile = TriggerBurrowMap.GetTile(X, Y);
            Tile.Walkable = true;
            Tile.Buildable = true;
            TriggerBurrowMap.SetTile(X, Y, Tile);
        }
    }
    Game::Systems::CommandSystem::SetMap(&TriggerBurrowMap);
    Game::Systems::CommandSystem TriggerBurrowCommands;
    TriggerBurrowCommands.Init(&TriggerBurrowSysMgr);

    const int TriggerBurrowedZergling = CreateUnit(
        TriggerBurrowEM, 37, 3.4f, {64.0f, 64.0f});
    auto& TriggerBurrowedPath =
        TriggerBurrowEM.AddComponent<Game::Components::PathfindingComponent>(
            TriggerBurrowedZergling);
    auto& TriggerBurrowedState =
        TriggerBurrowEM.AddComponent<Game::Components::BurrowComponent>(
            TriggerBurrowedZergling);
    TriggerBurrowedState.CanBurrow = true;
    TriggerBurrowedState.IsBurrowed = true;
    TriggerBurrowedState.Phase = Game::Components::BurrowPhase::Burrowed;

    Game::Components::Order TriggerUnburrowOrder;
    TriggerUnburrowOrder.Type = Game::Components::OrderType::Unburrow;
    Game::Systems::CommandSystem::IssueOrder(
        TriggerBurrowEM, TriggerBurrowedZergling, TriggerUnburrowOrder, false);
    Game::Components::Order TriggerMoveOrder;
    TriggerMoveOrder.Type = Game::Components::OrderType::AttackMove;
    TriggerMoveOrder.TargetPosition = {256.0f, 64.0f};
    Game::Systems::CommandSystem::IssueOrder(
        TriggerBurrowEM, TriggerBurrowedZergling, TriggerMoveOrder, true);

    auto* TriggerBurrowedCommand =
        TriggerBurrowEM.GetComponent<Game::Components::CommandComponent>(
            TriggerBurrowedZergling);
    Require(TriggerBurrowedCommand && TriggerBurrowedCommand->Orders.size() == 2 &&
            TriggerBurrowedCommand->GetCurrentOrder() &&
            TriggerBurrowedCommand->GetCurrentOrder()->Type ==
                Game::Components::OrderType::Unburrow,
        "campaign movement queues behind an Unburrow order");
    TriggerBurrowCommands.Update(0.05f);
    Require(TriggerBurrowedState.Phase ==
                Game::Components::BurrowPhase::Unburrowing &&
            TriggerBurrowedPath.Path.empty(),
        "queued campaign movement waits while UnBurrow iscript is active");

    TriggerBurrowedState.IsBurrowed = false;
    TriggerBurrowedState.Phase = Game::Components::BurrowPhase::Surface;
    TriggerBurrowCommands.Update(0.05f);
    Require(TriggerBurrowedCommand->CurrentCommand ==
                Game::Components::CommandType::AttackMove,
        "campaign attack-move becomes current after the UnBurrow signal completes");
    Require(TriggerBurrowedPath.RequestedDestination.X == 256.0f &&
            TriggerBurrowedPath.RequestedDestination.Y == 64.0f,
        "campaign movement restores its requested destination after UnBurrow");
    Require(!TriggerBurrowedPath.Path.empty(),
        "campaign movement resumes pathing after the UnBurrow signal completes");

    const int DirectUnburrowZergling = CreateUnit(
        TriggerBurrowEM, 37, 3.4f, {64.0f, 128.0f});
    auto& DirectUnburrowPath =
        TriggerBurrowEM.AddComponent<Game::Components::PathfindingComponent>(
            DirectUnburrowZergling);
    auto& DirectUnburrowState =
        TriggerBurrowEM.AddComponent<Game::Components::BurrowComponent>(
            DirectUnburrowZergling);
    DirectUnburrowState.CanBurrow = true;
    DirectUnburrowState.IsBurrowed = true;
    DirectUnburrowState.Phase = Game::Components::BurrowPhase::Burrowed;
    Require(Game::Systems::BurrowState::BeginTransition(
                TriggerBurrowEM, DirectUnburrowZergling, false),
        "direct UnBurrow transition starts without a rich order");
    Game::Components::Order DirectResumeAdvance;
    DirectResumeAdvance.Type = Game::Components::OrderType::AttackMove;
    DirectResumeAdvance.TargetPosition = {256.0f, 128.0f};
    Game::Systems::CommandSystem::IssueOrder(
        TriggerBurrowEM, DirectUnburrowZergling, DirectResumeAdvance, true);
    auto* DirectUnburrowCommand =
        TriggerBurrowEM.GetComponent<Game::Components::CommandComponent>(
            DirectUnburrowZergling);
    Require(DirectUnburrowCommand && DirectUnburrowCommand->Orders.size() == 2 &&
            DirectUnburrowCommand->GetCurrentOrder() &&
            DirectUnburrowCommand->GetCurrentOrder()->Type ==
                Game::Components::OrderType::Unburrow,
        "movement queues behind a directly-started UnBurrow transition");
    DirectUnburrowState.IsBurrowed = false;
    DirectUnburrowState.Phase = Game::Components::BurrowPhase::Surface;
    TriggerBurrowCommands.Update(0.05f);
    Require(DirectUnburrowCommand->CurrentCommand ==
                Game::Components::CommandType::AttackMove &&
            !DirectUnburrowPath.Path.empty(),
        "direct UnBurrow transition releases its queued attack-move at surface");
    Game::Systems::CommandSystem::SetMap(nullptr);

    int DeadScv = CreateUnit(
        EM,
        7,
        2.8f,
        {32.0f, 64.0f});
    auto* DeadScvUnit = EM.GetComponent<Game::Components::UnitComponent>(DeadScv);
    Require(DeadScvUnit != nullptr, "dead movement command coverage has unit data");
    DeadScvUnit->HitPoints = 0;
    auto& DeadScvPath = EM.AddComponent<Game::Components::PathfindingComponent>(DeadScv);
    DeadScvPath.Path.push_back({96.0f, 64.0f});

    Game::Systems::CommandSystem::IssueMove(EM, {DeadScv}, {160.0f, 64.0f});

    Require(DeadScvPath.Path.empty(),
        "dead units reject move orders and clear stale paths");
    Require(!EM.GetComponent<Game::Components::CommandComponent>(DeadScv),
        "dead units do not receive a move command component");

    int StasisScv = CreateUnit(
        EM,
        7,
        2.8f,
        {32.0f, 96.0f});
    auto& StasisPath = EM.AddComponent<Game::Components::PathfindingComponent>(StasisScv);
    StasisPath.Path.push_back({96.0f, 96.0f});
    auto& StasisStatus = EM.AddComponent<Game::Components::StatusEffectComponent>(StasisScv);
    StasisStatus.AddEffect(Game::Components::StatusEffect::Stasis, 5.0f, -1);

    Game::Systems::CommandSystem::IssueMove(EM, {StasisScv}, {160.0f, 96.0f});

    Require(StasisPath.Path.size() == 1 &&
            StasisPath.Path.front().X == 96.0f &&
            StasisPath.Path.front().Y == 96.0f,
        "stasis units reject new move orders without clearing the frozen path");
    Require(!EM.GetComponent<Game::Components::CommandComponent>(StasisScv),
        "stasis units do not receive a move command component");

    Game::Map::GameMap Map;
    Map.Generate(16, 16);
    Game::Map::Tile Blocked = Map.GetTile(8, 8);
    Blocked.Walkable = false;
    Blocked.Buildable = false;
    Map.SetTile(8, 8, Blocked);
    Game::Systems::CommandSystem::SetMap(&Map);

    int ScienceVessel = CreateUnit(
        EM,
        9,
        2.8f,
        {32.0f, 32.0f});
    auto& AirPath = EM.AddComponent<Game::Components::PathfindingComponent>(ScienceVessel);
    AirPath.IsAir = true;
    const Core::Math::Vec2 AirTarget{8.0f * 32.0f + 16.0f, 8.0f * 32.0f + 16.0f};

    Game::Systems::CommandSystem::IssueMove(EM, {ScienceVessel}, AirTarget);

    Require(AirPath.Path.size() == 1,
        "air units receive a direct movement waypoint instead of a ground A* path");
    Require(AirPath.Path.front().X == AirTarget.X && AirPath.Path.front().Y == AirTarget.Y,
        "air unit movement preserves the exact clicked target even on unwalkable terrain");

    int MutaliskWithoutPath = CreateUnit(
        EM,
        43,
        3.5f,
        {48.0f, 48.0f});
    Game::Systems::CommandSystem::IssueMove(EM, {MutaliskWithoutPath}, AirTarget);
    auto* MutaliskPath = EM.GetComponent<Game::Components::PathfindingComponent>(MutaliskWithoutPath);
    Require(MutaliskPath && MutaliskPath->IsAir,
        "air movement flag is inferred when an air unit had no path component");
    Require(MutaliskPath->Path.size() == 1 &&
            MutaliskPath->Path.front().X == AirTarget.X &&
            MutaliskPath->Path.front().Y == AirTarget.Y,
        "mutalisk without a prior path component still receives direct air movement");

    int TerrainScv = CreateUnit(
        EM,
        7,
        2.8f,
        {48.0f, 48.0f});
    Game::Systems::CommandSystem::IssueMove(EM, {TerrainScv}, AirTarget);
    auto* TerrainScvPath = EM.GetComponent<Game::Components::PathfindingComponent>(TerrainScv);
    Require(TerrainScvPath && !TerrainScvPath->IsAir,
        "ground movement flag is inferred for SCVs that had no path component");
    Require(TerrainScvPath->Path.empty() ||
            TerrainScvPath->Path.back().X != AirTarget.X ||
            TerrainScvPath->Path.back().Y != AirTarget.Y,
        "SCV movement does not receive a direct waypoint into blocked terrain");

    Game::Systems::CommandSystem::SetMap(nullptr);

    int StopTarget = CreateUnit(
        EM,
        0,
        0.0f,
        {256.0f, 128.0f});
    auto& StopTargetPlayer = EM.AddComponent<Game::Components::PlayerComponent>(StopTarget);
    StopTargetPlayer.PlayerID = 1;

    int ActiveMarine = CreateUnit(
        EM,
        0,
        4.0f,
        {128.0f, 128.0f});
    auto& ActiveCmd = EM.AddComponent<Game::Components::CommandComponent>(ActiveMarine);
    ActiveCmd.CurrentCommand = Game::Components::CommandType::Attack;
    ActiveCmd.TargetEntityID = StopTarget;
    Game::Components::Order QueuedOrder;
    QueuedOrder.Type = Game::Components::OrderType::Move;
    QueuedOrder.TargetPosition = {300.0f, 128.0f};
    ActiveCmd.QueueOrder(QueuedOrder);
    ActiveCmd.QueueOrder(QueuedOrder);

    auto& ActivePath = EM.AddComponent<Game::Components::PathfindingComponent>(ActiveMarine);
    ActivePath.Path.push_back({200.0f, 128.0f});
    ActivePath.PathIndex = 0;
    ActivePath.CurrentSpeed = 40.0f;
    ActivePath.CurrentVelocity = {40.0f, 0.0f};

    auto& ActiveCombat = EM.AddComponent<Game::Components::CombatComponent>(ActiveMarine);
    ActiveCombat.TargetEntityID = StopTarget;
    ActiveCombat.PendingAttackFrame = true;
    ActiveCombat.PendingAttackTargetEntityID = StopTarget;
    ActiveCombat.PendingAttackTargetPos = {256.0f, 128.0f};
    ActiveCombat.PendingAttackElapsed = 0.25f;

    Game::Client::GameLoop::SetAllCommand(
        EM, {ActiveMarine}, Game::Components::CommandType::Stop);
    Require(ActiveCmd.CurrentCommand == Game::Components::CommandType::Stop,
        "HUD stop command sets stop command immediately");
    Require(!ActiveCmd.HasPendingOrders(),
        "HUD stop command clears queued orders");
    Require(ActiveCmd.TargetEntityID == -1,
        "HUD stop command clears command target");
    Require(ActivePath.Path.empty() &&
            ActivePath.PathIndex == 0 &&
            ActivePath.CurrentSpeed == 0.0f &&
            ActivePath.CurrentVelocity.Length() == 0.0f,
        "HUD stop command clears active motion immediately");
    Require(ActiveCombat.TargetEntityID == -1 &&
            !ActiveCombat.PendingAttackFrame &&
            ActiveCombat.PendingAttackTargetEntityID == -1,
        "HUD stop command cancels combat target and pending attack frame");

    Core::ECS::SystemManager QueuedSysMgr;
    auto& QueuedEM = QueuedSysMgr.GetEntityManager();
    int QueuedScv = CreateUnit(
        QueuedEM,
        7,
        2.8f,
        {32.0f, 192.0f});
    Game::Components::Order FirstMove;
    FirstMove.Type = Game::Components::OrderType::Move;
    FirstMove.TargetPosition = {48.0f, 192.0f};
    Game::Components::Order SecondMove;
    SecondMove.Type = Game::Components::OrderType::Move;
    SecondMove.TargetPosition = {96.0f, 192.0f};
    Game::Systems::CommandSystem::IssueOrder(QueuedEM, QueuedScv, FirstMove, false);
    Game::Systems::CommandSystem::IssueOrder(QueuedEM, QueuedScv, SecondMove, true);

    auto* QueuedCmd = QueuedEM.GetComponent<Game::Components::CommandComponent>(QueuedScv);
    auto* QueuedPath = QueuedEM.GetComponent<Game::Components::PathfindingComponent>(QueuedScv);
    Require(QueuedCmd && QueuedCmd->Orders.size() == 2 && QueuedCmd->CurrentOrderIndex == 0,
        "queued move test starts with two orders and the first active");
    Require(QueuedPath && !QueuedPath->Path.empty() &&
            QueuedPath->Path.back().X == FirstMove.TargetPosition.X,
        "first queued move has an active path");

    Game::Systems::MovementSystem QueuedMovement(QueuedSysMgr);
    for (int I = 0; I < 20 && QueuedCmd->CurrentOrderIndex == 0; ++I)
        QueuedMovement.Update(0.05f);

    Require(QueuedCmd->CurrentOrderIndex == 1 &&
            QueuedCmd->CurrentCommand == Game::Components::CommandType::Move &&
            QueuedCmd->TargetPosition.X == SecondMove.TargetPosition.X,
        "movement completion advances to the next queued move order");
    Require(QueuedPath && !QueuedPath->Path.empty() &&
            QueuedPath->Path.back().X == SecondMove.TargetPosition.X,
        "advancing the queued move order generates the next movement path");

    for (int I = 0; I < 160 && QueuedCmd->HasPendingOrders(); ++I)
        QueuedMovement.Update(0.05f);

    auto* QueuedTrans = QueuedEM.GetComponent<Game::Components::TransformComponent>(QueuedScv);
    Require(QueuedCmd->CurrentOrderIndex == (int)QueuedCmd->Orders.size(),
        "queued move chain completes after the second destination");
    Require(QueuedPath && QueuedPath->Path.empty(),
        "queued move chain clears movement path after completion");
    Require(QueuedTrans &&
            QueuedTrans->Position.DistanceTo(SecondMove.TargetPosition) <= 16.25f,
        "queued move chain leaves the unit near the second destination");

    int FollowTarget = CreateUnit(
        EM,
        0,
        0.0f,
        {240.0f, 240.0f});
    int Follower = CreateUnit(
        EM,
        7,
        2.8f,
        {128.0f, 240.0f});
    Game::Components::Order FollowActive;
    FollowActive.Type = Game::Components::OrderType::Follow;
    FollowActive.TargetPosition = {240.0f, 240.0f};
    FollowActive.TargetEntityID = FollowTarget;
    Game::Systems::CommandSystem::IssueOrder(EM, Follower, FollowActive, false);
    auto* FollowCmd = EM.GetComponent<Game::Components::CommandComponent>(Follower);
    auto* FollowPath = EM.GetComponent<Game::Components::PathfindingComponent>(Follower);
    Require(FollowCmd && FollowCmd->CurrentCommand == Game::Components::CommandType::Follow,
        "follow order starts in follow command mode");
    Require(FollowPath, "follow order creates a path component");

    FollowPath->Path.clear();
    FollowPath->PathIndex = 0;
    auto* FollowTargetTrans = EM.GetComponent<Game::Components::TransformComponent>(FollowTarget);
    Require(FollowTargetTrans, "follow target has a transform");
    FollowTargetTrans->Position = {300.0f, 240.0f};
    Game::Systems::MovementSystem Movement(SysMgr);
    Movement.Update(0.05f);

    Require(FollowCmd->CurrentCommand == Game::Components::CommandType::Follow,
        "movement follow refresh preserves follow command instead of downgrading to move");
    Require(FollowCmd->TargetEntityID == FollowTarget,
        "movement follow refresh preserves the followed target entity");
    Require(FollowCmd->TargetPosition.X == FollowTargetTrans->Position.X &&
            FollowCmd->TargetPosition.Y == FollowTargetTrans->Position.Y,
        "movement follow refresh tracks the target's latest position");
    Require(FollowPath && !FollowPath->Path.empty(),
        "movement follow refresh generates a new path to the moving target");

    // Test Medic escort and auto-heal while following
    int MedicFollowTarget = CreateUnit(EM, 0, 0.0f, {200.0f, 200.0f}); // Marine
    int MedicFollower = CreateUnit(EM, 34, 2.8f, {150.0f, 200.0f}); // Medic
    EM.AddComponent<Game::Components::PlayerComponent>(MedicFollowTarget).PlayerID = 0;
    EM.AddComponent<Game::Components::PlayerComponent>(MedicFollower).PlayerID = 0;
    auto* MarineUnit = EM.GetComponent<Game::Components::UnitComponent>(MedicFollowTarget);
    auto* MedicUnit = EM.GetComponent<Game::Components::UnitComponent>(MedicFollower);
    MarineUnit->MaxHitPoints = 40;
    MarineUnit->HitPoints = 40;
    MedicUnit->EnergyPoints = 50;

    Game::Map::GameMap MedicTestMap;
    MedicTestMap.Generate(16, 16);
    Game::Systems::CommandSystem::SetMap(&MedicTestMap);
    Game::Systems::CommandSystem CmdSys;
    CmdSys.Init(&SysMgr);

    Game::Components::Order MedicFollowOrder;
    MedicFollowOrder.Type = Game::Components::OrderType::Follow;
    MedicFollowOrder.TargetPosition = {200.0f, 200.0f};
    MedicFollowOrder.TargetEntityID = MedicFollowTarget;
    Game::Systems::CommandSystem::IssueOrder(EM, MedicFollower, MedicFollowOrder, false);

    // Marine is damaged
    MarineUnit->HitPoints = 20;
    CmdSys.Update(0.05f);

    auto* MedicCmd = EM.GetComponent<Game::Components::CommandComponent>(MedicFollower);
    Require(MedicCmd && MedicCmd->GetCurrentOrder() &&
            MedicCmd->GetCurrentOrder()->Type == Game::Components::OrderType::Heal,
        "following medic automatically switches to Heal order when friendly unit is wounded");
    Require(MedicCmd->GetCurrentOrder()->TargetEntityID == MedicFollowTarget,
        "following medic heals the wounded friendly unit");

    Core::ECS::SystemManager LongMoveSysMgr;
    auto& LongMoveEM = LongMoveSysMgr.GetEntityManager();
    Game::Map::GameMap LongMoveMap;
    LongMoveMap.Generate(128, 16);
    Game::Systems::CommandSystem::SetMap(&LongMoveMap);
    const Core::Math::Vec2 LongMoveStart = LongMoveMap.TileToWorld(2, 8);
    const Core::Math::Vec2 LongMoveTarget = LongMoveMap.TileToWorld(118, 8);
    const int LongMoveMarine = CreateUnit(
        LongMoveEM, 0, 4.0f, LongMoveStart);
    Game::Components::Order LongAttackMove;
    LongAttackMove.Type = Game::Components::OrderType::AttackMove;
    LongAttackMove.TargetPosition = LongMoveTarget;
    Game::Systems::CommandSystem::IssueOrder(
        LongMoveEM, LongMoveMarine, LongAttackMove, false);
    auto* LongMovePath =
        LongMoveEM.GetComponent<Game::Components::PathfindingComponent>(LongMoveMarine);
    Require(LongMovePath && !LongMovePath->Path.empty(),
        "long attack-move creates its first bounded path leg");
    Require(LongMovePath->RequestedDestination.DistanceTo(LongMoveTarget) < 0.1f &&
            LongMovePath->Path.back().DistanceTo(LongMoveTarget) < 0.1f &&
            LongMovePath->LastPathStatus ==
                Game::Pathfinding::PathStatus::Exact,
        "long attack-move keeps an exact hierarchical route to its final destination");
    Game::Systems::MovementSystem LongMoveMovement(LongMoveSysMgr, &LongMoveMap);
    auto* LongMoveTransform =
        LongMoveEM.GetComponent<Game::Components::TransformComponent>(LongMoveMarine);
    for (int Step = 0;
         Step < 1600 && LongMoveTransform->Position.DistanceTo(LongMoveTarget) > 20.0f;
         ++Step)
    {
        LongMoveMovement.Update(0.05f);
    }
    Require(LongMoveTransform->Position.DistanceTo(LongMoveTarget) <= 20.0f,
        "long attack-move continues through staged legs to the final destination");

    Core::ECS::SystemManager DetourMoveSysMgr;
    auto& DetourMoveEM = DetourMoveSysMgr.GetEntityManager();
    Game::Map::GameMap DetourMoveMap;
    DetourMoveMap.Generate(128, 64);
    for (int Y = 0; Y <= 58; ++Y)
    {
        auto Wall = DetourMoveMap.GetTile(16, Y);
        Wall.Walkable = false;
        Wall.Buildable = false;
        DetourMoveMap.SetTile(16, Y, Wall);
    }
    Game::Systems::CommandSystem::SetMap(&DetourMoveMap);
    const Core::Math::Vec2 DetourStart = DetourMoveMap.TileToWorld(2, 8);
    const Core::Math::Vec2 DetourTarget = DetourMoveMap.TileToWorld(118, 8);
    const int DetourMarine = CreateUnit(
        DetourMoveEM, 0, 4.0f, DetourStart);
    Game::Components::Order DetourAttackMove;
    DetourAttackMove.Type = Game::Components::OrderType::AttackMove;
    DetourAttackMove.TargetPosition = DetourTarget;
    Game::Systems::CommandSystem::IssueOrder(
        DetourMoveEM, DetourMarine, DetourAttackMove, false);
    auto* DetourPath =
        DetourMoveEM.GetComponent<Game::Components::PathfindingComponent>(DetourMarine);
    Require(DetourPath && !DetourPath->Path.empty(),
        "long attack-move resolves a bounded stage beyond a deep wall detour");
    Game::Systems::MovementSystem DetourMovement(DetourMoveSysMgr, &DetourMoveMap);
    auto* DetourTransform =
        DetourMoveEM.GetComponent<Game::Components::TransformComponent>(DetourMarine);
    for (int Step = 0;
         Step < 3000 && DetourTransform->Position.DistanceTo(DetourTarget) > 20.0f;
         ++Step)
    {
        DetourMovement.Update(0.05f);
    }
    Require(DetourTransform->Position.DistanceTo(DetourTarget) <= 20.0f,
        "staged attack-move crosses a long detour without abandoning its final destination");

    Core::ECS::SystemManager BasePassageSysMgr;
    auto& BasePassageEM = BasePassageSysMgr.GetEntityManager();
    Game::Map::GameMap BasePassageMap;
    BasePassageMap.Generate(26, 18);
    Game::Systems::CommandSystem::SetMap(&BasePassageMap);

    const int UpperGateway = CreateUnit(
        BasePassageEM, 160, 0.0f, {400.0f, 208.0f});
    auto& UpperGatewayBuilding =
        BasePassageEM.AddComponent<Game::Components::BuildingComponent>(UpperGateway);
    UpperGatewayBuilding.IsBuilding = true;
    UpperGatewayBuilding.IsLifted = false;
    UpperGatewayBuilding.UnderConstruction = false;
    auto& UpperGatewayPath =
        BasePassageEM.AddComponent<Game::Components::PathfindingComponent>(UpperGateway);
    SetCollisionBounds(UpperGatewayPath, 48, 40, 56, 32);
    BasePassageEM.AddComponent<Game::Components::PlayerComponent>(UpperGateway).PlayerID = 0;

    const int LowerGateway = CreateUnit(
        BasePassageEM, 160, 0.0f, {400.0f, 336.0f});
    auto& LowerGatewayBuilding =
        BasePassageEM.AddComponent<Game::Components::BuildingComponent>(LowerGateway);
    LowerGatewayBuilding.IsBuilding = true;
    LowerGatewayBuilding.IsLifted = false;
    LowerGatewayBuilding.UnderConstruction = false;
    auto& LowerGatewayPath =
        BasePassageEM.AddComponent<Game::Components::PathfindingComponent>(LowerGateway);
    SetCollisionBounds(LowerGatewayPath, 48, 40, 56, 32);
    BasePassageEM.AddComponent<Game::Components::PlayerComponent>(LowerGateway).PlayerID = 0;

    const std::vector<Core::Math::Vec2> PassageStarts{
        {64.0f, 264.0f}, {80.0f, 280.0f},
        {80.0f, 264.0f}, {96.0f, 280.0f}};
    std::vector<int> PassageUnits;
    for (const auto& Start : PassageStarts)
    {
        const int PassageUnit = CreateUnit(BasePassageEM, 0, 4.0f, Start);
        auto& PassagePath =
            BasePassageEM.AddComponent<Game::Components::PathfindingComponent>(PassageUnit);
        SetCollisionBounds(PassagePath, 8, 9, 8, 10);
        BasePassageEM.AddComponent<Game::Components::PlayerComponent>(PassageUnit).PlayerID = 0;
        PassageUnits.push_back(PassageUnit);
    }

    const Core::Math::Vec2 PassageTarget{720.0f, 272.0f};
    Game::Systems::CommandSystem::IssueMove(
        BasePassageEM, PassageUnits, PassageTarget);
    std::vector<Core::Math::Vec2> PassageFinalTargets;
    for (int PassageUnit : PassageUnits)
    {
        const auto* PassagePath =
            BasePassageEM.GetComponent<Game::Components::PathfindingComponent>(PassageUnit);
        const auto* PassageCommand =
            BasePassageEM.GetComponent<Game::Components::CommandComponent>(PassageUnit);
        Require(PassagePath && !PassagePath->Path.empty() && PassageCommand,
            "a crowded group receives paths through a valid allied-building gap");
        PassageFinalTargets.push_back(PassageCommand->TargetPosition);
    }

    const auto* UpperGatewayTransform =
        BasePassageEM.GetComponent<Game::Components::TransformComponent>(UpperGateway);
    const auto* LowerGatewayTransform =
        BasePassageEM.GetComponent<Game::Components::TransformComponent>(LowerGateway);
    std::vector<bool> PassageCrossedGap(PassageUnits.size(), false);
    Game::Systems::MovementSystem BasePassageMovement(
        BasePassageSysMgr, &BasePassageMap);
    for (int Step = 0; Step < 1200; ++Step)
    {
        BasePassageMovement.Update(0.05f);
        bool AllArrived = true;
        for (size_t I = 0; I < PassageUnits.size(); ++I)
        {
            const auto* PassageTransform =
                BasePassageEM.GetComponent<Game::Components::TransformComponent>(
                    PassageUnits[I]);
            const auto* PassagePath =
                BasePassageEM.GetComponent<Game::Components::PathfindingComponent>(
                    PassageUnits[I]);
            Require(PassageTransform && PassagePath &&
                    !CollisionBoundsOverlap(
                        *PassageTransform, *PassagePath,
                        *UpperGatewayTransform, UpperGatewayPath) &&
                    !CollisionBoundsOverlap(
                        *PassageTransform, *PassagePath,
                        *LowerGatewayTransform, LowerGatewayPath),
                "group pathing through an allied base never crosses a building footprint");
            if (PassageTransform->Position.X >= 344.0f &&
                PassageTransform->Position.X <= 464.0f)
            {
                PassageCrossedGap[I] = true;
                Require(PassageTransform->Position.Y >= 250.0f &&
                        PassageTransform->Position.Y <= 286.0f,
                    "the allied-building gap keeps the unit inside the collision-safe corridor");
            }
            AllArrived = AllArrived &&
                PassageTransform->Position.DistanceTo(PassageFinalTargets[I]) <= 20.0f;
        }
        if (AllArrived)
            break;
    }
    for (size_t I = 0; I < PassageUnits.size(); ++I)
    {
        const auto* PassageTransform =
            BasePassageEM.GetComponent<Game::Components::TransformComponent>(
                PassageUnits[I]);
        Require(PassageCrossedGap[I] && PassageTransform &&
                PassageTransform->Position.DistanceTo(PassageFinalTargets[I]) <= 20.0f,
            "multiple allied units leave a narrow base passage without a permanent mutual block");
    }

    // A strict retry for a proven static blocker reuses the avoid parameter
    // that normally represents a dynamic unit. It must rasterize only that
    // static entity, not every stopped friendly unit elsewhere on the map.
    Core::ECS::SystemManager StrictStaticSysMgr;
    auto& StrictStaticEM = StrictStaticSysMgr.GetEntityManager();
    Game::Map::GameMap StrictStaticMap;
    StrictStaticMap.Generate(16, 8);
    Game::Systems::CommandSystem::SetMap(&StrictStaticMap);

    const int StrictMover = CreateUnit(
        StrictStaticEM, 64, 2.8f, {299.0f, 123.0f});
    auto& StrictMoverPath =
        StrictStaticEM.AddComponent<Game::Components::PathfindingComponent>(StrictMover);
    SetCollisionBounds(StrictMoverPath, 11, 11, 11, 11);
    StrictStaticEM.AddComponent<Game::Components::PlayerComponent>(StrictMover).PlayerID = 0;

    const int StrictMineral = CreateUnit(
        StrictStaticEM, 176, 0.0f, {192.0f, 144.0f});
    StrictStaticEM.AddComponent<Game::Components::ResourceComponent>(StrictMineral).Type =
        Game::Components::ResourceType::Minerals;
    auto& StrictMineralPath =
        StrictStaticEM.AddComponent<Game::Components::PathfindingComponent>(StrictMineral);
    SetCollisionBounds(StrictMineralPath, 32, 16, 31, 15);

    // The first route must run the same exact DAT segment preflight as runtime
    // collision. At y=123 the route enters the expanded mineral rectangle, but
    // its A* row is sampled at y=112 and would otherwise be returned as one
    // straight waypoint. Keep the requested mineral as the harvest exception
    // while strictly rerouting only around the adjacent mineral.
    const int RequestedMineral = CreateUnit(
        StrictStaticEM, 176, 0.0f, {64.0f, 144.0f});
    StrictStaticEM.AddComponent<Game::Components::ResourceComponent>(RequestedMineral).Type =
        Game::Components::ResourceType::Minerals;
    auto& RequestedMineralPath =
        StrictStaticEM.AddComponent<Game::Components::PathfindingComponent>(RequestedMineral);
    SetCollisionBounds(RequestedMineralPath, 32, 16, 31, 15);

    const int PreflightMover = CreateUnit(
        StrictStaticEM, 64, 2.8f, {267.0f, 123.0f});
    auto& PreflightMoverPath =
        StrictStaticEM.AddComponent<Game::Components::PathfindingComponent>(PreflightMover);
    SetCollisionBounds(PreflightMoverPath, 11, 11, 11, 11);
    PreflightMoverPath.TargetSpeed = 96.0f;
    PreflightMoverPath.Acceleration = 1000.0f;
    PreflightMoverPath.NodeTolerance = 4.0f;
    StrictStaticEM.AddComponent<Game::Components::PlayerComponent>(PreflightMover).PlayerID = 0;
    auto& PreflightHarvest =
        StrictStaticEM.AddComponent<Game::Components::HarvestComponent>(PreflightMover);
    PreflightHarvest.State = Game::Components::HarvestState::MovingToResource;
    PreflightHarvest.TargetResourceID = RequestedMineral;
    PreflightHarvest.DesiredResourceID = RequestedMineral;

    Require(Game::Systems::CommandSystem::RequestMovementPath(
                StrictStaticEM,
                PreflightMover,
                &StrictStaticMap,
                {64.0f, 123.0f},
                RequestedMineral),
        "initial Meltdown-shaped mineral route is corrected before movement starts");
    Require(PreflightMoverPath.RequestedIgnoreEntityID == RequestedMineral,
        "exact route preflight preserves the requested harvest mineral ignore");
    bool HasInitialLateralDetour = false;
    for (const auto& Waypoint : PreflightMoverPath.Path)
    {
        HasInitialLateralDetour = HasInitialLateralDetour ||
            std::fabs(Waypoint.Y - 123.0f) > 16.0f;
    }
    Require(HasInitialLateralDetour ||
            PreflightMoverPath.ResolvedPathDestination.DistanceTo(
                {64.0f, 123.0f}) <= 24.0f,
        "minitile route resolves a collision-safe mineral contact endpoint");

    Game::Systems::MovementSystem StrictStaticMovement(
        StrictStaticSysMgr, &StrictStaticMap);
    bool PreflightArrived = false;
    for (int Step = 0; Step < 240; ++Step)
    {
        StrictStaticMovement.Update(0.05f);
        const auto* PreflightTransform =
            StrictStaticEM.GetComponent<Game::Components::TransformComponent>(PreflightMover);
        Require(PreflightTransform &&
                PreflightMoverPath.RepathFailCount == 0 &&
                !CollisionBoundsOverlap(
                    *PreflightTransform, PreflightMoverPath,
                    *StrictStaticEM.GetComponent<Game::Components::TransformComponent>(
                        StrictMineral),
                    StrictMineralPath),
            "preflighted mineral route has no repath stall or DAT overlap");
        if (PreflightTransform->Position.DistanceTo({64.0f, 123.0f}) <= 20.0f)
        {
            PreflightArrived = true;
            break;
        }
    }
    Require(PreflightArrived,
        "preflighted Probe reaches its requested mineral on the first route");
    StrictStaticEM.DestroyEntity(PreflightMover);
    StrictStaticEM.DestroyEntity(RequestedMineral);

    const int RemoteStoppedFriend = CreateUnit(
        StrictStaticEM, 0, 4.0f, {2048.0f, 2048.0f});
    auto& RemoteStoppedPath =
        StrictStaticEM.AddComponent<Game::Components::PathfindingComponent>(RemoteStoppedFriend);
    SetCollisionBounds(RemoteStoppedPath, 4096, 4096, 4096, 4096);
    RemoteStoppedPath.CurrentVelocity = {0.0f, 0.0f};
    StrictStaticEM.AddComponent<Game::Components::PlayerComponent>(RemoteStoppedFriend).PlayerID = 0;

    Require(Game::Systems::CommandSystem::RequestMovementPath(
                StrictStaticEM,
                StrictMover,
                &StrictStaticMap,
                {64.0f, 123.0f},
                -1,
                StrictMineral),
        "strict static retry ignores unrelated stopped friendlies when building its route");
    Require(!StrictMoverPath.Path.empty() &&
            StrictMoverPath.RequestedIgnoreEntityID == -1,
        "strict static retry preserves interaction-ignore semantics while finding a detour");

    // A Probe already touching a mineral in a one-tile lane can have no exit
    // after the proven blocker receives strict half-tile raster padding. The
    // requester must return a real failure and preserve the blocking mineral;
    // accepting the original straight route here creates the observed
    // repath=0/1/2/0 harvest loop.
    Core::ECS::SystemManager SealedHarvestSysMgr;
    auto& SealedHarvestEM = SealedHarvestSysMgr.GetEntityManager();
    Game::Map::GameMap SealedHarvestMap;
    SealedHarvestMap.Generate(8, 5);
    for (int Y = 0; Y < SealedHarvestMap.GetHeight(); ++Y)
    {
        for (int X = 0; X < SealedHarvestMap.GetWidth(); ++X)
        {
            auto Tile = SealedHarvestMap.GetTile(X, Y);
            Tile.Walkable = false;
            SealedHarvestMap.SetTile(X, Y, Tile);
        }
    }
    for (int X = 1; X <= 5; ++X)
    {
        auto Tile = SealedHarvestMap.GetTile(X, 3);
        Tile.Walkable = true;
        SealedHarvestMap.SetTile(X, 3, Tile);
    }
    Game::Systems::CommandSystem::SetMap(&SealedHarvestMap);

    const int SealedProbe = CreateUnit(
        SealedHarvestEM, 64, 2.8f, {171.0f, 123.0f});
    auto& SealedProbePath =
        SealedHarvestEM.AddComponent<Game::Components::PathfindingComponent>(SealedProbe);
    SetCollisionBounds(SealedProbePath, 11, 11, 11, 11);
    SealedHarvestEM.AddComponent<Game::Components::PlayerComponent>(SealedProbe).PlayerID = 0;

    const int SealingMineral = CreateUnit(
        SealedHarvestEM, 178, 0.0f, {128.0f, 144.0f});
    auto& SealingResource =
        SealedHarvestEM.AddComponent<Game::Components::ResourceComponent>(SealingMineral);
    SealingResource.Type = Game::Components::ResourceType::Minerals;
    SealingResource.Amount = 1500;
    SealingResource.MaxAmount = 1500;
    auto& SealingMineralPath =
        SealedHarvestEM.AddComponent<Game::Components::PathfindingComponent>(SealingMineral);
    SetCollisionBounds(SealingMineralPath, 32, 16, 31, 15);

    const int SealedRequestedMineral = CreateUnit(
        SealedHarvestEM, 176, 0.0f, {48.0f, 144.0f});
    auto& SealedRequestedResource =
        SealedHarvestEM.AddComponent<Game::Components::ResourceComponent>(
            SealedRequestedMineral);
    SealedRequestedResource.Type = Game::Components::ResourceType::Minerals;
    SealedRequestedResource.Amount = 1500;
    SealedRequestedResource.MaxAmount = 1500;
    auto& SealedRequestedPath =
        SealedHarvestEM.AddComponent<Game::Components::PathfindingComponent>(
            SealedRequestedMineral);
    SetCollisionBounds(SealedRequestedPath, 32, 16, 31, 15);

    const int ExistingGatherer = SealedHarvestEM.CreateEntity();
    SealingResource.ActiveGathererEntityID = ExistingGatherer;
    auto& SealedHarvest =
        SealedHarvestEM.AddComponent<Game::Components::HarvestComponent>(SealedProbe);
    SealedHarvest.State = Game::Components::HarvestState::MovingToResource;
    SealedHarvest.TargetResourceID = SealedRequestedMineral;
    SealedHarvest.DesiredResourceID = SealedRequestedMineral;
    auto& SealedCommand =
        SealedHarvestEM.AddComponent<Game::Components::CommandComponent>(SealedProbe);
    SealedCommand.CurrentCommand = Game::Components::CommandType::Harvest;
    SealedCommand.TargetEntityID = SealedRequestedMineral;
    SealedCommand.TargetPosition = {48.0f, 144.0f};

    Require(Game::Systems::CommandSystem::RequestMovementPath(
                SealedHarvestEM,
                SealedProbe,
                &SealedHarvestMap,
                {48.0f, 123.0f},
                SealedRequestedMineral),
        "footprint-aware minitile path accepts a clear edge-contact route");
    Require(!SealedProbePath.Path.empty() &&
            SealedProbePath.LastPathStatus ==
                Game::Pathfinding::PathStatus::Partial &&
            !SealedProbePath.HasRecentMoveFailure &&
            SealedProbePath.RequestedIgnoreEntityID == SealedRequestedMineral,
        "interaction accepts a partial endpoint only when it is in contact range");

    Game::Systems::MovementSystem SealedMovement(
        SealedHarvestSysMgr, &SealedHarvestMap);
    Game::Systems::HarvestSystem SealedHarvestSystem(SealedHarvestSysMgr);
    for (int Step = 0; Step < 40; ++Step)
    {
        SealedMovement.Update(0.05f);
        SealedHarvestSystem.Update(0.05f);
    }
    Require(!SealedProbePath.HasRecentMoveFailure,
        "valid partial interaction route is not blacklisted as unreachable");

    // An interrupted worker can still be inside the resource contact box for
    // one frame. Exact preflight must preserve a route that exits that initial
    // overlap; otherwise construction repeatedly picks and refunds the same
    // trapped worker.
    Core::ECS::SystemManager ContactEscapeSysMgr;
    auto& ContactEscapeEM = ContactEscapeSysMgr.GetEntityManager();
    Game::Map::GameMap ContactEscapeMap;
    ContactEscapeMap.Generate(9, 5);
    for (int Y = 0; Y < ContactEscapeMap.GetHeight(); ++Y)
    {
        for (int X = 0; X < ContactEscapeMap.GetWidth(); ++X)
        {
            auto Tile = ContactEscapeMap.GetTile(X, Y);
            Tile.Walkable = Y == 3;
            ContactEscapeMap.SetTile(X, Y, Tile);
        }
    }
    Game::Systems::CommandSystem::SetMap(&ContactEscapeMap);
    const int ContactWorker = CreateUnit(
        ContactEscapeEM, 41, 2.8f, {144.0f, 123.0f});
    auto& ContactWorkerPath =
        ContactEscapeEM.AddComponent<Game::Components::PathfindingComponent>(
            ContactWorker);
    SetCollisionBounds(ContactWorkerPath, 11, 11, 11, 11);
    auto& ContactWorkerCommand =
        ContactEscapeEM.AddComponent<Game::Components::CommandComponent>(
            ContactWorker);
    ContactWorkerCommand.CurrentCommand = Game::Components::CommandType::Build;

    const int ContactMineral = CreateUnit(
        ContactEscapeEM, 178, 0.0f, {128.0f, 144.0f});
    ContactEscapeEM.AddComponent<Game::Components::ResourceComponent>(
        ContactMineral).Type = Game::Components::ResourceType::Minerals;
    auto& ContactMineralPath =
        ContactEscapeEM.AddComponent<Game::Components::PathfindingComponent>(
            ContactMineral);
    SetCollisionBounds(ContactMineralPath, 32, 16, 31, 15);

    Require(Game::Systems::CommandSystem::RequestMovementPath(
                ContactEscapeEM,
                ContactWorker,
                &ContactEscapeMap,
                {240.0f, 123.0f},
                -1),
        "construction worker can path outward from an interrupted resource contact");
    Game::Systems::MovementSystem ContactEscapeMovement(
        ContactEscapeSysMgr, &ContactEscapeMap);
    for (int Step = 0; Step < 20; ++Step)
        ContactEscapeMovement.Update(0.05f);
    const auto* ContactWorkerTransform =
        ContactEscapeEM.GetComponent<Game::Components::TransformComponent>(
            ContactWorker);
    Require(ContactWorkerTransform && ContactWorkerTransform->Position.X > 171.0f &&
            !ContactWorkerPath.HasRecentMoveFailure,
        "construction worker leaves the resource DAT rectangle without a retry loop");

    Core::ECS::SystemManager RepairSysMgr;
    auto& RepairEM = RepairSysMgr.GetEntityManager();
    Game::Map::GameMap RepairMap;
    RepairMap.Generate(16, 16);
    Game::Systems::CommandSystem::SetMap(&RepairMap);
    Game::Systems::CommandSystem RepairCommands;
    RepairCommands.Init(&RepairSysMgr);
    Game::Systems::MovementSystem RepairMovement(RepairSysMgr, &RepairMap);
    auto& RepairResources =
        Game::Systems::HarvestSystem::GetResources(0);
    RepairResources.Minerals = 400;
    RepairResources.VespeneGas = 100;

    const int RepairTarget = CreateUnit(
        RepairEM, 106, 0.0f, {160.0f, 96.0f});
    auto* RepairTargetUnit =
        RepairEM.GetComponent<Game::Components::UnitComponent>(RepairTarget);
    RepairTargetUnit->UnitRace = Game::Components::Race::Terran;
    RepairTargetUnit->HitPoints = 50;
    RepairTargetUnit->MaxHitPoints = 100;
    auto& RepairBuilding =
        RepairEM.AddComponent<Game::Components::BuildingComponent>(RepairTarget);
    RepairBuilding.UnderConstruction = false;
    auto& RepairTargetPath =
        RepairEM.AddComponent<Game::Components::PathfindingComponent>(RepairTarget);
    RepairTargetPath.HasCollisionBounds = true;
    RepairTargetPath.CollisionLeft = 63;
    RepairTargetPath.CollisionUp = 47;
    RepairTargetPath.CollisionRight = 64;
    RepairTargetPath.CollisionDown = 48;
    RepairEM.AddComponent<Game::Components::PlayerComponent>(RepairTarget).PlayerID = 0;

    const int RepairScv = CreateUnit(
        RepairEM, 7, 2.8f, {248.0f, 96.0f});
    RepairEM.AddComponent<Game::Components::PlayerComponent>(RepairScv).PlayerID = 0;
    Game::Components::Order RepairOrder;
    RepairOrder.Type = Game::Components::OrderType::Repair;
    RepairOrder.TargetEntityID = RepairTarget;
    RepairOrder.TargetPosition = {160.0f, 96.0f};
    RepairOrder.PathingIgnoreEntityID = RepairTarget;
    Game::Systems::CommandSystem::IssueOrder(
        RepairEM, RepairScv, RepairOrder, false);
    RepairCommands.Update(0.05f);
    Require(RepairTargetUnit->HitPoints == 50,
        "SCV repair rate is elapsed-time based instead of adding HP every update");
    for (int Step = 0; Step < 39; ++Step)
        RepairCommands.Update(0.05f);
    Require(RepairTargetUnit->HitPoints == 51,
        "SCV repairs a damaged friendly Terran building from its collision edge");

    RepairTargetUnit->HitPoints = 95;
    for (int Step = 0; Step < 200 &&
         RepairTargetUnit->HitPoints < RepairTargetUnit->MaxHitPoints; ++Step)
    {
        RepairCommands.Update(0.05f);
    }
    const auto* RepairCommand =
        RepairEM.GetComponent<Game::Components::CommandComponent>(RepairScv);
    Require(RepairTargetUnit->HitPoints == RepairTargetUnit->MaxHitPoints &&
            RepairCommand &&
            RepairCommand->CurrentCommand == Game::Components::CommandType::Stop,
        "repair completes at full health and releases the SCV");
    Require(RepairResources.Minerals < 400,
        "SCV repair consumes the owner's resources instead of restoring HP for free");

    const int RepairDebtTankA = CreateUnit(
        RepairEM, 5, 2.8f, {320.0f, 192.0f});
    auto* RepairDebtTankAUnit =
        RepairEM.GetComponent<Game::Components::UnitComponent>(RepairDebtTankA);
    RepairDebtTankAUnit->UnitRace = Game::Components::Race::Terran;
    RepairDebtTankAUnit->HitPoints = 130;
    RepairDebtTankAUnit->MaxHitPoints = 150;
    RepairEM.AddComponent<Game::Components::PlayerComponent>(RepairDebtTankA).PlayerID = 0;

    const int RepairDebtTankB = CreateUnit(
        RepairEM, 5, 2.8f, {384.0f, 192.0f});
    auto* RepairDebtTankBUnit =
        RepairEM.GetComponent<Game::Components::UnitComponent>(RepairDebtTankB);
    RepairDebtTankBUnit->UnitRace = Game::Components::Race::Terran;
    RepairDebtTankBUnit->HitPoints = 130;
    RepairDebtTankBUnit->MaxHitPoints = 150;
    RepairEM.AddComponent<Game::Components::PlayerComponent>(RepairDebtTankB).PlayerID = 0;

    const int RepairDebtScv = CreateUnit(
        RepairEM, 7, 2.8f, {352.0f, 192.0f});
    RepairEM.AddComponent<Game::Components::PlayerComponent>(RepairDebtScv).PlayerID = 0;
    Game::Components::Order RepairDebtOrder;
    RepairDebtOrder.Type = Game::Components::OrderType::Repair;
    RepairDebtOrder.TargetEntityID = RepairDebtTankA;
    RepairDebtOrder.TargetPosition = {320.0f, 192.0f};
    RepairDebtOrder.PathingIgnoreEntityID = RepairDebtTankA;
    const int RepairDebtStartMinerals = RepairResources.Minerals;
    const int RepairDebtStartGas = RepairResources.VespeneGas;
    Game::Systems::CommandSystem::IssueOrder(
        RepairEM, RepairDebtScv, RepairDebtOrder, false);
    for (int Step = 0; Step < 7; ++Step)
        RepairCommands.Update(0.05f);
    Require(RepairDebtTankAUnit->HitPoints == 131,
        "repair debt regression starts with exactly one repaired tank hit point");

    const auto* RepairDebtScvUnit =
        RepairEM.GetComponent<Game::Components::UnitComponent>(RepairDebtScv);
    const float MineralDebtAfterFirstPoint =
        RepairDebtScvUnit->RepairMineralCostAccumulator;
    const float GasDebtAfterFirstPoint =
        RepairDebtScvUnit->RepairGasCostAccumulator;
    Game::Components::Order CancelRepairDebt;
    CancelRepairDebt.Type = Game::Components::OrderType::Stop;
    Game::Systems::CommandSystem::IssueOrder(
        RepairEM, RepairDebtScv, CancelRepairDebt, false);
    RepairCommands.Update(0.05f);
    Require(NearlyEqual(
                RepairDebtScvUnit->RepairMineralCostAccumulator,
                MineralDebtAfterFirstPoint) &&
            NearlyEqual(
                RepairDebtScvUnit->RepairGasCostAccumulator,
                GasDebtAfterFirstPoint),
        "cancelling repair preserves fractional mineral and gas debt");

    for (int RepairIndex = 1; RepairIndex < 5; ++RepairIndex)
    {
        const bool RepairA = (RepairIndex % 2) == 0;
        RepairDebtOrder.TargetEntityID =
            RepairA ? RepairDebtTankA : RepairDebtTankB;
        RepairDebtOrder.TargetPosition =
            RepairA ? Core::Math::Vec2{320.0f, 192.0f}
                    : Core::Math::Vec2{384.0f, 192.0f};
        RepairDebtOrder.PathingIgnoreEntityID = RepairDebtOrder.TargetEntityID;
        Game::Systems::CommandSystem::IssueOrder(
            RepairEM, RepairDebtScv, RepairDebtOrder, false);
        for (int Step = 0; Step < 7; ++Step)
            RepairCommands.Update(0.05f);
    }
    Require(RepairDebtTankAUnit->HitPoints + RepairDebtTankBUnit->HitPoints == 265,
        "switching repair targets applies five independently timed hit points");
    Require(RepairResources.Minerals == RepairDebtStartMinerals - 1 &&
            RepairResources.VespeneGas == RepairDebtStartGas - 1,
        "cancel-and-switch repair pays accumulated fractional mineral and gas costs");

    const int DamagedTank = CreateUnit(
        RepairEM, 5, 2.8f, {304.0f, 96.0f});
    auto* DamagedTankUnit =
        RepairEM.GetComponent<Game::Components::UnitComponent>(DamagedTank);
    DamagedTankUnit->UnitRace = Game::Components::Race::Terran;
    DamagedTankUnit->HitPoints = 80;
    DamagedTankUnit->MaxHitPoints = 150;
    RepairEM.AddComponent<Game::Components::PlayerComponent>(DamagedTank).PlayerID = 0;
    RepairOrder.TargetEntityID = DamagedTank;
    RepairOrder.TargetPosition = {304.0f, 96.0f};
    RepairOrder.PathingIgnoreEntityID = DamagedTank;
    Game::Systems::CommandSystem::IssueOrder(
        RepairEM, RepairScv, RepairOrder, false);
    auto* RepairScvTransform =
        RepairEM.GetComponent<Game::Components::TransformComponent>(RepairScv);
    RepairScvTransform->Position = {280.0f, 96.0f};
    for (int Step = 0; Step < 7; ++Step)
        RepairCommands.Update(0.05f);
    Require(DamagedTankUnit->HitPoints == 81,
        "SCV repair order supports damaged friendly Terran mechanical units");

    const int MovingRepairTarget = CreateUnit(
        RepairEM, 5, 2.8f, {160.0f, 320.0f});
    auto* MovingRepairUnit =
        RepairEM.GetComponent<Game::Components::UnitComponent>(MovingRepairTarget);
    MovingRepairUnit->UnitRace = Game::Components::Race::Terran;
    MovingRepairUnit->HitPoints = 40;
    MovingRepairUnit->MaxHitPoints = 150;
    auto& MovingRepairTargetPath =
        RepairEM.AddComponent<Game::Components::PathfindingComponent>(MovingRepairTarget);
    MovingRepairTargetPath.CollisionRadius = 16;
    RepairEM.AddComponent<Game::Components::PlayerComponent>(MovingRepairTarget).PlayerID = 0;

    const int ChasingRepairScv = CreateUnit(
        RepairEM, 7, 2.8f, {48.0f, 320.0f});
    RepairEM.AddComponent<Game::Components::PlayerComponent>(ChasingRepairScv).PlayerID = 0;
    Game::Components::Order MovingRepairOrder;
    MovingRepairOrder.Type = Game::Components::OrderType::Repair;
    MovingRepairOrder.TargetEntityID = MovingRepairTarget;
    MovingRepairOrder.TargetPosition = {160.0f, 320.0f};
    MovingRepairOrder.PathingIgnoreEntityID = MovingRepairTarget;
    Game::Systems::CommandSystem::IssueOrder(
        RepairEM, ChasingRepairScv, MovingRepairOrder, false);

    auto* MovingRepairTransform =
        RepairEM.GetComponent<Game::Components::TransformComponent>(MovingRepairTarget);
    MovingRepairTransform->Position = {416.0f, 320.0f};
    RepairCommands.Update(0.05f);
    auto* ChasingRepairPath =
        RepairEM.GetComponent<Game::Components::PathfindingComponent>(ChasingRepairScv);
    Require(ChasingRepairPath && !ChasingRepairPath->Path.empty() &&
            ChasingRepairPath->RequestedTargetEntityID == MovingRepairTarget &&
            ChasingRepairPath->RequestedDestination.DistanceTo(
                MovingRepairTransform->Position) < 0.1f,
        "SCV repair refreshes its entity-target destination when the target moves");

    for (int Step = 0; Step < 900 && MovingRepairUnit->HitPoints < MovingRepairUnit->MaxHitPoints;
         ++Step)
    {
        RepairMovement.Update(0.05f);
        RepairCommands.Update(0.05f);
    }
    auto* ChasingRepairTransform =
        RepairEM.GetComponent<Game::Components::TransformComponent>(ChasingRepairScv);
    Require(MovingRepairUnit->HitPoints == MovingRepairUnit->MaxHitPoints &&
            ChasingRepairTransform->Position.DistanceTo(
                MovingRepairTransform->Position) < 48.0f,
        "SCV follows a moved mechanical target and completes its repair");

    // End-to-end SCV mineral harvest + Command Center delivery cycle test
    // using real CommandSystem + NavigationService + MovementSystem + HarvestSystem.
    {
        Core::ECS::SystemManager HarvestCycleSysMgr;
        auto& HarvestCycleEM = HarvestCycleSysMgr.GetEntityManager();
        Game::Map::GameMap HarvestCycleMap;
        HarvestCycleMap.Generate(24, 24);
        Game::Systems::CommandSystem::SetMap(&HarvestCycleMap);
        Game::Systems::CommandSystem HarvestCycleCommands;
        HarvestCycleCommands.Init(&HarvestCycleSysMgr);
        Game::Systems::MovementSystem HarvestCycleMovement(
            HarvestCycleSysMgr, &HarvestCycleMap);
        Game::Systems::HarvestSystem HarvestCycleHarvest(HarvestCycleSysMgr);

        constexpr int CyclePlayerID = 3;
        auto& CycleResources =
            Game::Systems::HarvestSystem::GetResources(CyclePlayerID);
        CycleResources.Minerals = 50;
        CycleResources.VespeneGas = 0;

        const int CycleCommandCenter = CreateUnit(
            HarvestCycleEM, 106, 0.0f, {384.0f, 384.0f});
        auto* CycleCcUnit =
            HarvestCycleEM.GetComponent<Game::Components::UnitComponent>(
                CycleCommandCenter);
        CycleCcUnit->UnitRace = Game::Components::Race::Terran;
        CycleCcUnit->HitPoints = 1500;
        CycleCcUnit->MaxHitPoints = 1500;
        auto& CycleCcBuilding =
            HarvestCycleEM.AddComponent<Game::Components::BuildingComponent>(
                CycleCommandCenter);
        CycleCcBuilding.IsBuilding = true;
        CycleCcBuilding.IsLifted = false;
        CycleCcBuilding.UnderConstruction = false;
        CycleCcBuilding.BuildingDefID =
            Game::Races::GetUnitDefID("CommandCenter");
        auto& CycleCcPath =
            HarvestCycleEM.AddComponent<Game::Components::PathfindingComponent>(
                CycleCommandCenter);
        SetCollisionBounds(CycleCcPath, 64, 48, 63, 47);
        HarvestCycleEM.AddComponent<Game::Components::PlayerComponent>(
            CycleCommandCenter).PlayerID = CyclePlayerID;

        const int CycleMineralA = CreateUnit(
            HarvestCycleEM, 176, 0.0f, {160.0f, 352.0f});
        auto& CycleMineralARes =
            HarvestCycleEM.AddComponent<Game::Components::ResourceComponent>(
                CycleMineralA);
        CycleMineralARes.Type = Game::Components::ResourceType::Minerals;
        CycleMineralARes.Amount = 1500;
        CycleMineralARes.MaxAmount = 1500;
        auto& CycleMineralAPath =
            HarvestCycleEM.AddComponent<Game::Components::PathfindingComponent>(
                CycleMineralA);
        SetCollisionBounds(CycleMineralAPath, 32, 16, 31, 15);

        const int CycleMineralB = CreateUnit(
            HarvestCycleEM, 176, 0.0f, {160.0f, 416.0f});
        auto& CycleMineralBRes =
            HarvestCycleEM.AddComponent<Game::Components::ResourceComponent>(
                CycleMineralB);
        CycleMineralBRes.Type = Game::Components::ResourceType::Minerals;
        CycleMineralBRes.Amount = 1500;
        CycleMineralBRes.MaxAmount = 1500;
        auto& CycleMineralBPath =
            HarvestCycleEM.AddComponent<Game::Components::PathfindingComponent>(
                CycleMineralB);
        SetCollisionBounds(CycleMineralBPath, 32, 16, 31, 15);

        std::vector<int> CycleScvs;
        for (int Index = 0; Index < 2; ++Index)
        {
            const int CycleScv = CreateUnit(
                HarvestCycleEM, 7, 2.8f,
                {288.0f, 360.0f + (float)Index * 48.0f});
            auto& CycleScvPath =
                HarvestCycleEM.AddComponent<Game::Components::PathfindingComponent>(
                    CycleScv);
            SetCollisionBounds(CycleScvPath, 11, 11, 11, 11);
            HarvestCycleEM.AddComponent<Game::Components::PlayerComponent>(
                CycleScv).PlayerID = CyclePlayerID;
            auto& CycleScvHarvest =
                HarvestCycleEM.AddComponent<Game::Components::HarvestComponent>(
                    CycleScv);
            CycleScvHarvest.MineInterval = 0.2f;
            CycleScvs.push_back(CycleScv);
        }

        Require(Game::Systems::HarvestSystem::IssueHarvest(
                    HarvestCycleEM, CycleScvs, CycleMineralA),
            "SCVs accept mineral harvest order on real map");

        for (int Step = 0; Step < 900 && CycleResources.Minerals < 50 + 32; ++Step)
        {
            HarvestCycleCommands.Update(0.05f);
            HarvestCycleMovement.Update(0.05f);
            HarvestCycleHarvest.Update(0.05f);
            for (int CycleScv : CycleScvs)
            {
                const auto* Cmd =
                    HarvestCycleEM.GetComponent<Game::Components::CommandComponent>(
                        CycleScv);
                Require(Cmd &&
                        Cmd->CurrentCommand == Game::Components::CommandType::Harvest,
                    "active SCV harvester preserves CommandType::Harvest throughout gather and return cycles");
            }
        }

        Require(CycleResources.Minerals >= 50 + 32,
            "SCVs complete multiple mineral harvest and Command Center delivery cycles without getting stuck");

        // Now place SCVs at the upper-left delivery zone inside the Command Center
        // (344, 352) and at the top-left corner band (319.2, 330.0), and order them
        // to harvest the lower mineral patch (160, 416) so their departure trajectory
        // moves diagonally down-left across the Command Center's expanded/unexpanded bounds.
        const int BeforeCornerMinerals = CycleResources.Minerals;
        HarvestCycleEM.GetComponent<Game::Components::TransformComponent>(
            CycleScvs[0])->Position = {344.0f, 352.0f};
        HarvestCycleEM.GetComponent<Game::Components::TransformComponent>(
            CycleScvs[1])->Position = {319.2f, 330.0f};
        for (int CycleScv : CycleScvs)
        {
            auto* Harvest =
                HarvestCycleEM.GetComponent<Game::Components::HarvestComponent>(
                    CycleScv);
            Harvest->State = Game::Components::HarvestState::Idle;
            Harvest->CarryingAmount = 0;
        }
        Require(Game::Systems::HarvestSystem::IssueHarvest(
                    HarvestCycleEM, CycleScvs, CycleMineralB),
            "SCVs inside Command Center delivery zone and corner band accept harvest order to lower mineral");

        HarvestCycleCommands.Update(0.05f);
        HarvestCycleMovement.Update(0.05f);
        {
            const auto* Pos0 =
                HarvestCycleEM.GetComponent<Game::Components::TransformComponent>(
                    CycleScvs[0]);
            const auto* Pos1 =
                HarvestCycleEM.GetComponent<Game::Components::TransformComponent>(
                    CycleScvs[1]);
            const auto* Path0 =
                HarvestCycleEM.GetComponent<Game::Components::PathfindingComponent>(
                    CycleScvs[0]);
            const auto* Path1 =
                HarvestCycleEM.GetComponent<Game::Components::PathfindingComponent>(
                    CycleScvs[1]);
            Require(Pos0 && Pos1 && Path0 && Path1 &&
                    Pos0->Position.X < 343.5f &&
                    Pos1->Position.X < 318.8f &&
                    Path0->BlockedByEntityID < 0 &&
                    Path1->BlockedByEntityID < 0,
                "SCVs inside Command Center delivery zone and corner band immediately step toward lower mineral without static-blocker stall");
        }
        HarvestCycleHarvest.Update(0.05f);

        bool BothReachedMineral = false;
        for (int Step = 0;
             Step < 900 &&
             (CycleResources.Minerals < BeforeCornerMinerals + 32 || !BothReachedMineral);
             ++Step)
        {
            HarvestCycleCommands.Update(0.05f);
            HarvestCycleMovement.Update(0.05f);
            HarvestCycleHarvest.Update(0.05f);
            const auto* Pos0 =
                HarvestCycleEM.GetComponent<Game::Components::TransformComponent>(
                    CycleScvs[0]);
            const auto* Pos1 =
                HarvestCycleEM.GetComponent<Game::Components::TransformComponent>(
                    CycleScvs[1]);
            if (Pos0 && Pos1 && Pos0->Position.X < 240.0f && Pos1->Position.X < 240.0f)
            {
                BothReachedMineral = true;
            }
        }
        Require(BothReachedMineral &&
                CycleResources.Minerals >= BeforeCornerMinerals + 32,
            "SCVs departing Command Center delivery zone and corner band diagonally to lower mineral do not get trapped");

        // Partially surround the Command Center with adjacent buildings on the
        // North and East sides (forming a sealed pocket on the East side while
        // only the West side faces the mineral line) and place a stationary unit
        // mid-route so harvesters must repath mid-route without losing their
        // mineral/depot contact segment.
        const auto AddAdjacentSupplyDepot = [&](Core::Math::Vec2 Pos,
                                                int Left, int Up, int Right, int Down)
        {
            const int Depot = CreateUnit(HarvestCycleEM, 109, 0.0f, Pos);
            auto* DepotUnit =
                HarvestCycleEM.GetComponent<Game::Components::UnitComponent>(Depot);
            DepotUnit->UnitRace = Game::Components::Race::Terran;
            DepotUnit->HitPoints = 500;
            DepotUnit->MaxHitPoints = 500;
            auto& DepotBld =
                HarvestCycleEM.AddComponent<Game::Components::BuildingComponent>(Depot);
            DepotBld.IsBuilding = true;
            DepotBld.IsLifted = false;
            DepotBld.UnderConstruction = false;
            DepotBld.BuildingDefID = Game::Races::GetUnitDefID("SupplyDepot");
            auto& DepotPath =
                HarvestCycleEM.AddComponent<Game::Components::PathfindingComponent>(Depot);
            SetCollisionBounds(DepotPath, Left, Up, Right, Down);
            HarvestCycleEM.AddComponent<Game::Components::PlayerComponent>(Depot).PlayerID =
                CyclePlayerID;
            return Depot;
        };
        AddAdjacentSupplyDepot({352.0f, 304.0f}, 32, 24, 31, 23); // North of Command Center
        AddAdjacentSupplyDepot({472.0f, 328.0f}, 32, 8, 31, 7);   // East pocket top wall
        AddAdjacentSupplyDepot({472.0f, 440.f}, 32, 8, 31, 7);    // East pocket bottom wall
        AddAdjacentSupplyDepot({504.0f, 384.0f}, 8, 64, 7, 63);   // East pocket right wall

        const int MidRouteBlocker = CreateUnit(
            HarvestCycleEM, 0, 0.0f, {272.0f, 416.0f});
        auto& BlockerPath =
            HarvestCycleEM.AddComponent<Game::Components::PathfindingComponent>(
                MidRouteBlocker);
        SetCollisionBounds(BlockerPath, 11, 11, 11, 11);
        HarvestCycleEM.AddComponent<Game::Components::PlayerComponent>(
            MidRouteBlocker).PlayerID = CyclePlayerID;

        const int BeforePocketMinerals = CycleResources.Minerals;
        HarvestCycleEM.GetComponent<Game::Components::TransformComponent>(
            CycleScvs[0])->Position = {408.0f, 384.0f};
        HarvestCycleEM.GetComponent<Game::Components::TransformComponent>(
            CycleScvs[1])->Position = {344.0f, 352.0f};
        for (int CycleScv : CycleScvs)
        {
            auto* Harvest =
                HarvestCycleEM.GetComponent<Game::Components::HarvestComponent>(
                    CycleScv);
            Harvest->State = Game::Components::HarvestState::Idle;
            Harvest->CarryingAmount = 0;
        }
        Require(Game::Systems::HarvestSystem::IssueHarvest(
                    HarvestCycleEM, CycleScvs, CycleMineralB),
            "SCVs in partially surrounded Command Center accept harvest order");

        for (int Step = 0;
             Step < 900 && CycleResources.Minerals < BeforePocketMinerals + 32;
             ++Step)
        {
            HarvestCycleCommands.Update(0.05f);
            HarvestCycleMovement.Update(0.05f);
            HarvestCycleHarvest.Update(0.05f);
        }
        Require(CycleResources.Minerals >= BeforePocketMinerals + 32,
            "SCVs harvest and deliver across multiple cycles with partially surrounded Command Center and mid-route blocker");
        Game::Systems::CommandSystem::SetMap(nullptr);
    }

    std::printf("CommandSystemOrderTest passed\n");
    return 0;
}
