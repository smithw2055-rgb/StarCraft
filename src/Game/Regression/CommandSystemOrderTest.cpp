#include "Game/Systems/CommandSystem.h"

#include "Core/ECS/SystemManager.h"
#include "Game/DataEnums.h"
#include "Game/Map/Map.h"
#include "Game/Systems/CombatSystem.h"

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

    std::printf("CommandSystemOrderTest passed\n");
    return 0;
}
