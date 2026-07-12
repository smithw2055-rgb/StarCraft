#include "Game/Systems/CommandSystem.h"

#include "Core/ECS/SystemManager.h"
#include "Game/DataEnums.h"
#include "Game/Map/Map.h"
#include "Game/Systems/CombatSystem.h"
#include "Main/GameLoop/InputHandler.h"
#include "Game/Systems/MovementSystem.h"

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

    int Scv = CreateUnit(
        EM,
        7,
        2.8f,
        {32.0f, 32.0f});

    Game::Systems::CommandSystem::IssueMove(EM, {Scv}, {160.0f, 32.0f});

    auto* ScvCmd = EM.GetComponent<Game::Components::CommandComponent>(Scv);
    auto* ScvPath = EM.GetComponent<Game::Components::PathfindingComponent>(Scv);
    Require(ScvCmd && ScvCmd->CurrentCommand == Game::Components::CommandType::Move,
        "mobile units receive move commands");
    Require(ScvPath && !ScvPath->Path.empty(),
        "mobile units receive movement paths");

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

    const int SpiderMine = CreateUnit(EM, 13, 10.0f, {96.0f, 224.0f});
    auto& SpiderMinePath =
        EM.AddComponent<Game::Components::PathfindingComponent>(SpiderMine);
    SpiderMinePath.Path.push_back({128.0f, 224.0f});
    EM.AddComponent<Game::Components::SpiderMineComponent>(SpiderMine).IsPlaced = true;
    Game::Systems::CommandSystem::IssueMove(EM, {SpiderMine}, {320.0f, 224.0f});
    Require(SpiderMinePath.Path.empty() &&
            !EM.GetComponent<Game::Components::CommandComponent>(SpiderMine),
        "placed Spider Mine rejects player movement orders");

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

    int QueuedScv = CreateUnit(
        EM,
        7,
        2.8f,
        {32.0f, 192.0f});
    Game::Components::Order FirstMove;
    FirstMove.Type = Game::Components::OrderType::Move;
    FirstMove.TargetPosition = {48.0f, 192.0f};
    Game::Components::Order SecondMove;
    SecondMove.Type = Game::Components::OrderType::Move;
    SecondMove.TargetPosition = {96.0f, 192.0f};
    Game::Systems::CommandSystem::IssueOrder(EM, QueuedScv, FirstMove, false);
    Game::Systems::CommandSystem::IssueOrder(EM, QueuedScv, SecondMove, true);

    auto* QueuedCmd = EM.GetComponent<Game::Components::CommandComponent>(QueuedScv);
    auto* QueuedPath = EM.GetComponent<Game::Components::PathfindingComponent>(QueuedScv);
    Require(QueuedCmd && QueuedCmd->Orders.size() == 2 && QueuedCmd->CurrentOrderIndex == 0,
        "queued move test starts with two orders and the first active");
    Require(QueuedPath && !QueuedPath->Path.empty() &&
            QueuedPath->Path.back().X == FirstMove.TargetPosition.X,
        "first queued move has an active path");

    Game::Systems::MovementSystem Movement(SysMgr);
    for (int I = 0; I < 20 && QueuedCmd->CurrentOrderIndex == 0; ++I)
        Movement.Update(0.05f);

    Require(QueuedCmd->CurrentOrderIndex == 1 &&
            QueuedCmd->CurrentCommand == Game::Components::CommandType::Move &&
            QueuedCmd->TargetPosition.X == SecondMove.TargetPosition.X,
        "movement completion advances to the next queued move order");
    Require(QueuedPath && !QueuedPath->Path.empty() &&
            QueuedPath->Path.back().X == SecondMove.TargetPosition.X,
        "advancing the queued move order generates the next movement path");

    for (int I = 0; I < 40 && QueuedCmd->HasPendingOrders(); ++I)
        Movement.Update(0.05f);

    auto* QueuedTrans = EM.GetComponent<Game::Components::TransformComponent>(QueuedScv);
    Require(QueuedCmd->CurrentOrderIndex == (int)QueuedCmd->Orders.size(),
        "queued move chain completes after the second destination");
    Require(QueuedPath && QueuedPath->Path.empty(),
        "queued move chain clears movement path after completion");
    Require(QueuedTrans &&
            QueuedTrans->Position.DistanceTo(SecondMove.TargetPosition) < 16.0f,
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

    std::printf("CommandSystemOrderTest passed\n");
    return 0;
}
