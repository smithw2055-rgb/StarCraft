#include "Game/Systems/MovementSystem.h"

#include "Game/Systems/CommandSystem.h"
#include "Game/Map/Map.h"
#include "Game/Pathfinding/AStar.h"
#include "Game/DataEnums.h"
#include "Core/ECS/SystemManager.h"

#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <vector>

namespace Game::Systems
{

void CommandSystem::IssueMove(Core::ECS::EntityManager& EM,
                              const std::vector<Core::ECS::EntityID>& Entities,
                              const Core::Math::Vec2& Target)
{
    for (Core::ECS::EntityID EntityID : Entities)
    {
        auto* Cmd = EM.GetComponent<Components::CommandComponent>(EntityID);
        if (!Cmd)
            Cmd = &EM.AddComponent<Components::CommandComponent>(EntityID);
        Cmd->CurrentCommand = Components::CommandType::Move;
        Cmd->TargetPosition = Target;

        auto* Path = EM.GetComponent<Components::PathfindingComponent>(EntityID);
        if (!Path)
            Path = &EM.AddComponent<Components::PathfindingComponent>(EntityID);
        Path->Path.clear();
        Path->Path.push_back(Target);
        Path->PathIndex = 0;
    }
}

void CommandSystem::AdvanceQueue(Core::ECS::EntityManager&, Core::ECS::EntityID)
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

    int CreateMovableUnit(Core::ECS::EntityManager& EM,
                          Core::Math::Vec2 Pos,
                          Core::Math::Vec2 Target,
                          int ChkUnitID,
                          Game::Components::UnitSize Size,
                          int CollisionRadius)
    {
        int UnitID = EM.CreateEntity();
        EM.AddComponent<Game::Components::TransformComponent>(UnitID).Position = Pos;

        auto& Unit = EM.AddComponent<Game::Components::UnitComponent>(UnitID);
        Unit.ChkUnitID = ChkUnitID;
        Unit.Speed = 2.8f;
        Unit.SizeType = Size;

        auto& Path = EM.AddComponent<Game::Components::PathfindingComponent>(UnitID);
        Path.TargetSpeed = 64.0f;
        Path.Acceleration = 512.0f;
        Path.NodeTolerance = 1.0f;
        Path.CollisionRadius = CollisionRadius;
        Path.Path.push_back(Target);

        auto& Cmd = EM.AddComponent<Game::Components::CommandComponent>(UnitID);
        Cmd.CurrentCommand = Game::Components::CommandType::Move;
        Cmd.TargetPosition = Target;
        return UnitID;
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
        Path.CollisionRadius = std::max(4, std::max(Left + Right + 1, Up + Down + 1) / 2);
    }
}

int main()
{
    Game::Map::GameMap Map;
    Map.Generate(16, 16);
    const Core::Math::Vec2 OffCenterStart{33.0f, 33.0f};
    const Core::Math::Vec2 ExactTarget{320.0f, 33.0f};
    auto PathResult = Game::Pathfinding::AStar::FindPath(Map, OffCenterStart, ExactTarget);
    Require(PathResult.Found, "A* finds a path on an open map");
    Require(!PathResult.Waypoints.empty(), "A* returns at least one movement waypoint");
    int FirstTileX = -1;
    int FirstTileY = -1;
    Map.WorldToTile(PathResult.Waypoints.front(), FirstTileX, FirstTileY);
    Require(FirstTileX != 1 || FirstTileY != 1,
        "A* omits the start tile center so single units do not step backward");
    Require(PathResult.Waypoints.back().X == ExactTarget.X &&
            PathResult.Waypoints.back().Y == ExactTarget.Y,
        "A* preserves the exact move target when the destination tile is walkable");

    Game::Map::GameMap NarrowMap;
    NarrowMap.Generate(5, 3);
    for (int Y = 0; Y < 3; ++Y)
    {
        for (int X = 0; X < 5; ++X)
        {
            auto Tile = NarrowMap.GetTile(X, Y);
            Tile.Walkable = (Y == 1);
            NarrowMap.SetTile(X, Y, Tile);
        }
    }
    auto SmallCorridorPath = Game::Pathfinding::AStar::FindPath(
        NarrowMap, {48.0f, 48.0f}, {112.0f, 48.0f});
    Require(SmallCorridorPath.Found,
        "A* allows center-based small units through a one-tile corridor");
    auto LargeCorridorPath = Game::Pathfinding::AStar::FindPath(
        NarrowMap, {48.0f, 48.0f}, {112.0f, 48.0f}, 1000, 20);
    Require(!LargeCorridorPath.Found,
        "A* rejects one-tile corridors for large units that need terrain clearance");

    Core::ECS::SystemManager SysMgr;
    auto& EM = SysMgr.GetEntityManager();
    Game::Systems::MovementSystem Movement(SysMgr);

    int UnitID = EM.CreateEntity();
    auto& Trans = EM.AddComponent<Game::Components::TransformComponent>(UnitID);
    Trans.Position = {0.0f, 0.0f};

    auto& Path = EM.AddComponent<Game::Components::PathfindingComponent>(UnitID);
    Path.TargetSpeed = 64.0f;
    Path.Acceleration = 512.0f;
    Path.NodeTolerance = 1.0f;
    Path.Path.push_back({256.0f, 0.0f});

    auto& Cmd = EM.AddComponent<Game::Components::CommandComponent>(UnitID);
    Cmd.CurrentCommand = Game::Components::CommandType::Stop;

    Movement.Update(1.0f);

    Require(Path.Path.empty(), "Stop command clears an active movement path");
    Require(Path.PathIndex == 0, "Stop command resets path index");
    Require(Path.CurrentSpeed == 0.0f, "Stop command resets movement speed");
    Require(Path.CurrentVelocity.Length() == 0.0f, "Stop command resets velocity");
    Require(Trans.Position.X == 0.0f && Trans.Position.Y == 0.0f,
        "Stop command prevents movement along stale path");

    Core::ECS::SystemManager CrowdSysMgr;
    auto& CrowdEM = CrowdSysMgr.GetEntityManager();
    Game::Systems::MovementSystem CrowdMovement(CrowdSysMgr);

    const Core::Math::Vec2 Target{256.0f, 0.0f};
    int Front = CreateMovableUnit(CrowdEM, {0.0f, 0.0f}, Target,
                                  0, Game::Components::UnitSize::Small, 16);
    int Middle = CreateMovableUnit(CrowdEM, {-10.0f, 0.0f}, Target,
                                   7, Game::Components::UnitSize::Small, 16);
    int Back = CreateMovableUnit(CrowdEM, {-22.0f, 0.0f}, Target,
                                 5, Game::Components::UnitSize::Large, 24);

    CrowdMovement.Update(0.1f);

    auto* FrontTrans = CrowdEM.GetComponent<Game::Components::TransformComponent>(Front);
    auto* MiddleTrans = CrowdEM.GetComponent<Game::Components::TransformComponent>(Middle);
    auto* BackTrans = CrowdEM.GetComponent<Game::Components::TransformComponent>(Back);
    Require(FrontTrans && MiddleTrans && BackTrans, "crowd test units keep transforms");
    Require(FrontTrans->Position.X > 0.0f,
        "front unit advances toward shared move target");
    Require(MiddleTrans->Position.X > -10.0f,
        "middle unit is not pushed backward by overlap repulsion");
    Require(BackTrans->Position.X > -22.0f,
        "rear large unit is not pushed backward by overlap repulsion");

    Core::ECS::SystemManager ImmobileSysMgr;
    auto& ImmobileEM = ImmobileSysMgr.GetEntityManager();
    Game::Systems::MovementSystem ImmobileMovement(ImmobileSysMgr);

    int CommandCenter = ImmobileEM.CreateEntity();
    auto& CcTrans = ImmobileEM.AddComponent<Game::Components::TransformComponent>(CommandCenter);
    CcTrans.Position = {64.0f, 64.0f};
    auto& CcUnit = ImmobileEM.AddComponent<Game::Components::UnitComponent>(CommandCenter);
    CcUnit.ChkUnitID = 106; // Terran Command Center raw CHK id.
    CcUnit.Speed = 0.0f;
    CcUnit.SizeType = Game::Components::UnitSize::Large;
    auto& CcBuilding = ImmobileEM.AddComponent<Game::Components::BuildingComponent>(CommandCenter);
    CcBuilding.IsBuilding = true;
    CcBuilding.IsLifted = false;
    auto& CcPath = ImmobileEM.AddComponent<Game::Components::PathfindingComponent>(CommandCenter);
    CcPath.TargetSpeed = 64.0f;
    CcPath.Path.push_back({256.0f, 64.0f});
    ImmobileEM.AddComponent<Game::Components::CommandComponent>(CommandCenter).CurrentCommand =
        Game::Components::CommandType::Move;

    ImmobileMovement.Update(0.2f);
    Require(CcTrans.Position.X == 64.0f && CcTrans.Position.Y == 64.0f,
        "landed buildings ignore stale or accidental movement paths");
    Require(CcPath.Path.empty(), "landed building movement path is cleared");

    int SiegedTank = CreateMovableUnit(
        ImmobileEM, {64.0f, 128.0f}, {256.0f, 128.0f},
        30,
        Game::Components::UnitSize::Large, 24);
    auto* TankTrans = ImmobileEM.GetComponent<Game::Components::TransformComponent>(SiegedTank);
    auto* TankPath = ImmobileEM.GetComponent<Game::Components::PathfindingComponent>(SiegedTank);
    ImmobileMovement.Update(0.2f);
    Require(TankTrans && TankTrans->Position.X == 64.0f,
        "sieged tank does not move until unsieged");
    Require(TankPath && TankPath->Path.empty(),
        "sieged tank movement path is cleared");

    Core::ECS::SystemManager BlockerSysMgr;
    auto& BlockerEM = BlockerSysMgr.GetEntityManager();
    Game::Systems::MovementSystem BlockerMovement(BlockerSysMgr);
    int Mover = CreateMovableUnit(BlockerEM, {0.0f, 0.0f}, {256.0f, 0.0f},
                                  0, Game::Components::UnitSize::Small, 16);
    int Building = BlockerEM.CreateEntity();
    BlockerEM.AddComponent<Game::Components::TransformComponent>(Building).Position = {96.0f, 0.0f};
    auto& BUnit = BlockerEM.AddComponent<Game::Components::UnitComponent>(Building);
    BUnit.ChkUnitID = 111; // Terran Barracks raw CHK id.
    BUnit.SizeType = Game::Components::UnitSize::Large;
    BUnit.Speed = 0.0f;
    auto& Bldg = BlockerEM.AddComponent<Game::Components::BuildingComponent>(Building);
    Bldg.IsBuilding = true;
    Bldg.IsLifted = false;

    for (int I = 0; I < 40; ++I)
        BlockerMovement.Update(0.05f);

    auto* MoverTrans = BlockerEM.GetComponent<Game::Components::TransformComponent>(Mover);
    Require(MoverTrans && MoverTrans->Position.X < 40.0f,
        "ground units are blocked by landed buildings instead of passing through them");

    Core::ECS::SystemManager DatBlockerSysMgr;
    auto& DatBlockerEM = DatBlockerSysMgr.GetEntityManager();
    Game::Systems::MovementSystem DatBlockerMovement(DatBlockerSysMgr);
    int DatMover = CreateMovableUnit(DatBlockerEM, {0.0f, 0.0f}, {256.0f, 0.0f},
                                     0, Game::Components::UnitSize::Small, 8);
    int DatBuilding = DatBlockerEM.CreateEntity();
    DatBlockerEM.AddComponent<Game::Components::TransformComponent>(DatBuilding).Position =
        {96.0f, 0.0f};
    auto& DatBUnit = DatBlockerEM.AddComponent<Game::Components::UnitComponent>(DatBuilding);
    DatBUnit.ChkUnitID = 111; // Terran Barracks raw CHK id.
    DatBUnit.SizeType = Game::Components::UnitSize::Large;
    DatBUnit.Speed = 0.0f;
    auto& DatBldg = DatBlockerEM.AddComponent<Game::Components::BuildingComponent>(DatBuilding);
    DatBldg.IsBuilding = true;
    DatBldg.IsLifted = false;
    auto& DatBPath = DatBlockerEM.AddComponent<Game::Components::PathfindingComponent>(DatBuilding);
    SetCollisionBounds(DatBPath, 48, 40, 56, 32);

    for (int I = 0; I < 40; ++I)
        DatBlockerMovement.Update(0.05f);

    auto* DatMoverTrans = DatBlockerEM.GetComponent<Game::Components::TransformComponent>(DatMover);
    Require(DatMoverTrans && DatMoverTrans->Position.X <= 40.1f,
        "ground units use units.dat asymmetric building bounds for static blockers");

    Core::ECS::SystemManager TerrainSysMgr;
    auto& TerrainEM = TerrainSysMgr.GetEntityManager();
    Game::Map::GameMap TerrainMap;
    TerrainMap.Generate(6, 6);
    Game::Map::Tile BlockedTile = TerrainMap.GetTile(2, 1);
    BlockedTile.Walkable = false;
    TerrainMap.SetTile(2, 1, BlockedTile);
    Game::Systems::MovementSystem TerrainMovement(TerrainSysMgr, &TerrainMap);

    int GroundMover = CreateMovableUnit(TerrainEM, {48.0f, 48.0f}, {96.0f, 48.0f},
                                        7, Game::Components::UnitSize::Small, 12);
    for (int I = 0; I < 20; ++I)
        TerrainMovement.Update(0.05f);
    auto* GroundTrans = TerrainEM.GetComponent<Game::Components::TransformComponent>(GroundMover);
    Require(GroundTrans && GroundTrans->Position.X < 64.0f,
        "ground movement cannot step into unwalkable terrain even with a stale direct path");

    int AirMover = CreateMovableUnit(TerrainEM, {48.0f, 96.0f}, {96.0f, 96.0f},
                                     9,
                                     Game::Components::UnitSize::Large, 20);
    auto* AirPath = TerrainEM.GetComponent<Game::Components::PathfindingComponent>(AirMover);
    Require(AirPath, "air movement test unit has path component");
    AirPath->IsAir = true;
    for (int I = 0; I < 20; ++I)
        TerrainMovement.Update(0.05f);
    auto* AirTrans = TerrainEM.GetComponent<Game::Components::TransformComponent>(AirMover);
    Require(AirTrans && AirTrans->Position.X > 64.0f,
        "air movement ignores ground terrain walkability");

    Core::ECS::SystemManager CorridorSysMgr;
    auto& CorridorEM = CorridorSysMgr.GetEntityManager();
    Game::Systems::MovementSystem CorridorMovement(CorridorSysMgr, &NarrowMap);
    int SmallCorridorUnit = CreateMovableUnit(
        CorridorEM, {48.0f, 48.0f}, {112.0f, 48.0f},
        0, Game::Components::UnitSize::Small, 12);
    int LargeCorridorUnit = CreateMovableUnit(
        CorridorEM, {48.0f, 48.0f}, {112.0f, 48.0f},
        30, Game::Components::UnitSize::Large, 20);
    for (int I = 0; I < 10; ++I)
        CorridorMovement.Update(0.05f);
    auto* SmallCorridorTrans =
        CorridorEM.GetComponent<Game::Components::TransformComponent>(SmallCorridorUnit);
    auto* LargeCorridorTrans =
        CorridorEM.GetComponent<Game::Components::TransformComponent>(LargeCorridorUnit);
    Require(SmallCorridorTrans && SmallCorridorTrans->Position.X > 48.0f,
        "small ground unit can move through a one-tile walkable corridor");
    Require(LargeCorridorTrans && LargeCorridorTrans->Position.X <= 48.1f,
        "large ground unit does not squeeze into a one-tile walkable corridor");

    std::printf("MovementCommandTest passed\n");
    return 0;
}
