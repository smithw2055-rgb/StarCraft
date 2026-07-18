#include "Game/Systems/MovementSystem.h"

#include "Game/Systems/CommandSystem.h"
#include "Game/Map/Map.h"
#include "Game/Pathfinding/AStar.h"
#include "Game/DataEnums.h"
#include "Core/ECS/SystemManager.h"

#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <utility>
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
        Path->RequestedDestination = Target;
        Path->RequestedTargetEntityID = -1;
        Path->RequestedIgnoreEntityID = -1;
        Path->DestinationStallTimer = 0.0f;
        Path->HasLastDestinationDistance = false;
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
        Path.RequestedDestination = Target;
        Path.RequestedTargetEntityID = -1;
        Path.RequestedIgnoreEntityID = -1;

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

    int CreateStationaryCollisionUnit(Core::ECS::EntityManager& EM,
                                      Core::Math::Vec2 Position,
                                      int Left,
                                      int Up,
                                      int Right,
                                      int Down)
    {
        int UnitID = EM.CreateEntity();
        EM.AddComponent<Game::Components::TransformComponent>(UnitID).Position = Position;
        auto& Unit = EM.AddComponent<Game::Components::UnitComponent>(UnitID);
        Unit.ChkUnitID = 0;
        Unit.Speed = 4.0f;
        auto& Path = EM.AddComponent<Game::Components::PathfindingComponent>(UnitID);
        SetCollisionBounds(Path, Left, Up, Right, Down);
        EM.AddComponent<Game::Components::CommandComponent>(UnitID).CurrentCommand =
            Game::Components::CommandType::Stop;
        return UnitID;
    }

    float CollisionBoundsOverlapArea(
        const Game::Components::TransformComponent& ATrans,
        const Game::Components::PathfindingComponent& APath,
        const Game::Components::TransformComponent& BTrans,
        const Game::Components::PathfindingComponent& BPath)
    {
        const float ALeft = ATrans.Position.X - (float)APath.CollisionLeft;
        const float ARight = ATrans.Position.X + (float)APath.CollisionRight + 1.0f;
        const float ATop = ATrans.Position.Y - (float)APath.CollisionUp;
        const float ABottom = ATrans.Position.Y + (float)APath.CollisionDown + 1.0f;
        const float BLeft = BTrans.Position.X - (float)BPath.CollisionLeft;
        const float BRight = BTrans.Position.X + (float)BPath.CollisionRight + 1.0f;
        const float BTop = BTrans.Position.Y - (float)BPath.CollisionUp;
        const float BBottom = BTrans.Position.Y + (float)BPath.CollisionDown + 1.0f;
        const float OverlapX = std::min(ARight, BRight) - std::max(ALeft, BLeft);
        const float OverlapY = std::min(ABottom, BBottom) - std::max(ATop, BTop);
        return std::max(0.0f, OverlapX) * std::max(0.0f, OverlapY);
    }

    bool CollisionBoundsOverlap(const Game::Components::TransformComponent& ATrans,
                                const Game::Components::PathfindingComponent& APath,
                                const Game::Components::TransformComponent& BTrans,
                                const Game::Components::PathfindingComponent& BPath)
    {
        return CollisionBoundsOverlapArea(ATrans, APath, BTrans, BPath) > 0.0f;
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

    Game::Map::GameMap ClearanceRecoveryMap;
    ClearanceRecoveryMap.Generate(6, 6);
    for (int Y = 0; Y < ClearanceRecoveryMap.GetHeight(); ++Y)
        for (int X = 0; X < ClearanceRecoveryMap.GetWidth(); ++X)
        {
            auto Tile = ClearanceRecoveryMap.GetTile(X, Y);
            Tile.Walkable = true;
            ClearanceRecoveryMap.SetTile(X, Y, Tile);
        }
    auto ClearanceBlock = ClearanceRecoveryMap.GetTile(2, 0);
    ClearanceBlock.Walkable = false;
    ClearanceRecoveryMap.SetTile(2, 0, ClearanceBlock);
    auto ClearanceRecoveryPath = Game::Pathfinding::AStar::FindPath(
        ClearanceRecoveryMap,
        ClearanceRecoveryMap.TileToWorld(2, 1),
        ClearanceRecoveryMap.TileToWorld(2, 4),
        1000, 20);
    Require(ClearanceRecoveryPath.Found && !ClearanceRecoveryPath.Waypoints.empty(),
        "A* lets a unit escape a walkable start tile that temporarily fails radius clearance");

    Game::Map::GameMap UnwalkableStartRecoveryMap;
    UnwalkableStartRecoveryMap.Generate(5, 5);
    auto UnwalkableStartTile = UnwalkableStartRecoveryMap.GetTile(2, 2);
    UnwalkableStartTile.Walkable = false;
    UnwalkableStartRecoveryMap.SetTile(2, 2, UnwalkableStartTile);
    auto UnwalkableStartRecoveryPath = Game::Pathfinding::AStar::FindPath(
        UnwalkableStartRecoveryMap,
        UnwalkableStartRecoveryMap.TileToWorld(2, 2),
        UnwalkableStartRecoveryMap.TileToWorld(2, 1));
    Require(UnwalkableStartRecoveryPath.Found &&
            !UnwalkableStartRecoveryPath.Waypoints.empty(),
        "A* lets a displaced unit escape an unwalkable occupied start tile");

    Game::Map::GameMap ElevationRampMap;
    ElevationRampMap.Generate(7, 5);
    for (int Y = 0; Y < ElevationRampMap.GetHeight(); ++Y)
    {
        for (int X = 0; X < ElevationRampMap.GetWidth(); ++X)
        {
            auto Tile = ElevationRampMap.GetTile(X, Y);
            Tile.Walkable = true;
            Tile.GroundHeight = X <= 2 ? 1 : 0;
            Tile.SubtileGroundHeights.fill(Tile.GroundHeight);
            if (X == 3)
            {
                Tile.Walkable = Y == 4;
                Tile.GroundHeight = Y == 4 ? 1 : 0;
                Tile.SubtileGroundHeights.fill(Tile.GroundHeight);
            }
            ElevationRampMap.SetTile(X, Y, Tile);
        }
    }
    auto ElevationRampPath = Game::Pathfinding::AStar::FindPath(
        ElevationRampMap,
        ElevationRampMap.TileToWorld(1, 1),
        ElevationRampMap.TileToWorld(5, 1));
    Require(ElevationRampPath.Found && !ElevationRampPath.Waypoints.empty(),
        "A* reaches low ground from high ground through a legal middle-height ramp");
    bool UsesElevationRamp = false;
    for (const auto& Waypoint : ElevationRampPath.Waypoints)
    {
        int WX = 0;
        int WY = 0;
        ElevationRampMap.WorldToTile(Waypoint, WX, WY);
        UsesElevationRamp = UsesElevationRamp || (WX == 3 && WY == 4);
    }
    Require(UsesElevationRamp,
        "height-aware smoothing keeps the ramp instead of cutting across a cliff");

    Game::Map::GameMap DisconnectedElevationMap = ElevationRampMap;
    auto RemovedRamp = DisconnectedElevationMap.GetTile(3, 4);
    RemovedRamp.Walkable = false;
    DisconnectedElevationMap.SetTile(3, 4, RemovedRamp);
    auto DisconnectedElevationPath = Game::Pathfinding::AStar::FindPath(
        DisconnectedElevationMap,
        DisconnectedElevationMap.TileToWorld(1, 1),
        DisconnectedElevationMap.TileToWorld(5, 1));
    Require(!DisconnectedElevationPath.Found,
        "A* does not replace an unreachable low-ground click with nearby high ground");

    Game::Map::GameMap BlockedGoalMap;
    BlockedGoalMap.Generate(24, 7);
    for (int Y = 0; Y < BlockedGoalMap.GetHeight(); ++Y)
    {
        for (int X = 12; X <= 22; ++X)
        {
            auto Tile = BlockedGoalMap.GetTile(X, Y);
            Tile.Walkable = false;
            Tile.Buildable = false;
            BlockedGoalMap.SetTile(X, Y, Tile);
        }
    }
    auto BlockedGoalPath = Game::Pathfinding::AStar::FindPath(
        BlockedGoalMap, BlockedGoalMap.TileToWorld(2, 3), BlockedGoalMap.TileToWorld(17, 3));
    Require(BlockedGoalPath.Found,
        "A* resolves a blocked target to the nearest reachable walkable tile");
    Require(!BlockedGoalPath.Waypoints.empty(),
        "blocked-target A* result still returns a movement waypoint");
    int BlockedGoalEndX = -1;
    int BlockedGoalEndY = -1;
    BlockedGoalMap.WorldToTile(BlockedGoalPath.Waypoints.back(), BlockedGoalEndX, BlockedGoalEndY);
    Require(BlockedGoalEndX == 11 && BlockedGoalEndY == 3,
        "blocked-target A* walks to the closest reachable edge outside the obstacle");

    Game::Map::GameMap LongCorridorMap;
    LongCorridorMap.Generate(48, 3);
    for (int Y = 0; Y < LongCorridorMap.GetHeight(); ++Y)
    {
        for (int X = 0; X < LongCorridorMap.GetWidth(); ++X)
        {
            auto Tile = LongCorridorMap.GetTile(X, Y);
            Tile.Walkable = (Y == 1);
            LongCorridorMap.SetTile(X, Y, Tile);
        }
    }
    auto LongCorridorPath = Game::Pathfinding::AStar::FindPath(
        LongCorridorMap,
        LongCorridorMap.TileToWorld(1, 1),
        LongCorridorMap.TileToWorld(46, 1),
        3);
    Require(LongCorridorPath.Found,
        "A* searches the reachable grid even when the caller passes a tiny node budget");

    auto SavedConfig = Game::Pathfinding::AStar::GetConfig();
    Game::Pathfinding::AStar::SearchConfig NoCornerCutConfig = SavedConfig;
    NoCornerCutConfig.AllowDiagonal = true;
    NoCornerCutConfig.CutCorners = false;
    Game::Pathfinding::AStar::SetConfig(NoCornerCutConfig);

    Game::Map::GameMap SealedCornerMap;
    SealedCornerMap.Generate(2, 2);
    for (const auto& BlockedTile : std::vector<std::pair<int, int>>{{1, 0}, {0, 1}})
    {
        auto Tile = SealedCornerMap.GetTile(BlockedTile.first, BlockedTile.second);
        Tile.Walkable = false;
        SealedCornerMap.SetTile(BlockedTile.first, BlockedTile.second, Tile);
    }
    auto SealedCornerPath = Game::Pathfinding::AStar::FindPath(
        SealedCornerMap,
        SealedCornerMap.TileToWorld(0, 0),
        SealedCornerMap.TileToWorld(1, 1));
    if (!SealedCornerPath.Waypoints.empty())
    {
        int SealedEndX = -1;
        int SealedEndY = -1;
        SealedCornerMap.WorldToTile(SealedCornerPath.Waypoints.back(), SealedEndX, SealedEndY);
        Require(SealedEndX != 1 || SealedEndY != 1,
            "A* does not cut through a blocked diagonal corner");
    }

    Game::Map::GameMap CornerDetourMap;
    CornerDetourMap.Generate(4, 3);
    auto CornerBlock = CornerDetourMap.GetTile(1, 0);
    CornerBlock.Walkable = false;
    CornerDetourMap.SetTile(1, 0, CornerBlock);
    auto CornerDetourPath = Game::Pathfinding::AStar::FindPath(
        CornerDetourMap,
        CornerDetourMap.TileToWorld(0, 0),
        CornerDetourMap.TileToWorld(3, 0));
    Require(CornerDetourPath.Found,
        "A* finds a legal detour when diagonal corner cutting is blocked");

    Game::Map::GameMap SmoothingDetourMap;
    SmoothingDetourMap.Generate(7, 5);
    for (int Y = 0; Y < 4; ++Y)
    {
        auto Tile = SmoothingDetourMap.GetTile(3, Y);
        Tile.Walkable = false;
        SmoothingDetourMap.SetTile(3, Y, Tile);
    }
    auto SmoothingDetourPath = Game::Pathfinding::AStar::FindPath(
        SmoothingDetourMap,
        SmoothingDetourMap.TileToWorld(1, 1),
        SmoothingDetourMap.TileToWorld(5, 1));
    Require(SmoothingDetourPath.Found && SmoothingDetourPath.Waypoints.size() >= 2,
        "A* smoothing keeps bend waypoints when the direct line is blocked");
    bool UsesWallGap = false;
    for (const auto& Waypoint : SmoothingDetourPath.Waypoints)
    {
        int WX = 0;
        int WY = 0;
        SmoothingDetourMap.WorldToTile(Waypoint, WX, WY);
        UsesWallGap = UsesWallGap || WY == 4;
    }
    Require(UsesWallGap,
        "smoothed A* route goes around the wall through its reachable gap");
    Game::Pathfinding::AStar::SetConfig(SavedConfig);

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

    Core::ECS::SystemManager EnsnareSysMgr;
    auto& EnsnareEM = EnsnareSysMgr.GetEntityManager();
    Game::Systems::MovementSystem EnsnareMovement(EnsnareSysMgr);
    int EnsnaredUnit = CreateMovableUnit(
        EnsnareEM, {0.0f, 32.0f}, {256.0f, 32.0f},
        0, Game::Components::UnitSize::Small, 16);
    auto& EnsnareStatus =
        EnsnareEM.AddComponent<Game::Components::StatusEffectComponent>(EnsnaredUnit);
    EnsnareStatus.AddEffect(Game::Components::StatusEffect::Ensnare, 8.0f, -1);
    auto* EnsnarePath =
        EnsnareEM.GetComponent<Game::Components::PathfindingComponent>(EnsnaredUnit);
    Require(EnsnarePath, "ensnared movement test unit has path component");
    EnsnareMovement.Update(0.1f);
    Require(EnsnarePath->CurrentSpeed == 32.0f,
        "Ensnare halves movement target speed while the status is active");
    EnsnareStatus.RemoveEffect(Game::Components::StatusEffect::Ensnare);
    EnsnareMovement.Update(0.1f);
    Require(EnsnarePath->CurrentSpeed == 64.0f,
        "movement speed returns to normal after Ensnare is removed");
    EnsnareStatus.AddEffect(Game::Components::StatusEffect::Stim, 8.0f, -1);
    EnsnarePath->Acceleration = 1024.0f;
    EnsnareMovement.Update(0.1f);
    Require(EnsnarePath->CurrentSpeed == 96.0f,
        "Stim raises movement target speed while the status is active");
    EnsnareStatus.AddEffect(Game::Components::StatusEffect::Ensnare, 8.0f, -1);
    EnsnareMovement.Update(0.1f);
    Require(EnsnarePath->CurrentSpeed == 64.0f,
        "Stim and Ensnare movement modifiers neutralize each other");

    Core::ECS::SystemManager StasisSysMgr;
    auto& StasisEM = StasisSysMgr.GetEntityManager();
    Game::Systems::MovementSystem StasisMovement(StasisSysMgr);
    int StasisUnit = CreateMovableUnit(
        StasisEM, {0.0f, 64.0f}, {256.0f, 64.0f},
        7, Game::Components::UnitSize::Small, 16);
    auto& StasisStatus =
        StasisEM.AddComponent<Game::Components::StatusEffectComponent>(StasisUnit);
    StasisStatus.AddEffect(Game::Components::StatusEffect::Stasis, 8.0f, -1);
    auto* StasisPath =
        StasisEM.GetComponent<Game::Components::PathfindingComponent>(StasisUnit);
    auto* StasisTrans =
        StasisEM.GetComponent<Game::Components::TransformComponent>(StasisUnit);
    Require(StasisPath && StasisTrans, "stasis movement test unit has path and transform data");
    StasisMovement.Update(0.2f);
    Require(StasisTrans->Position.X == 0.0f && StasisTrans->Position.Y == 64.0f,
        "Stasis pauses movement without advancing along the active path");
    Require(!StasisPath->Path.empty() && StasisPath->PathIndex == 0,
        "Stasis preserves the active movement path for resume");
    Require(StasisPath->CurrentSpeed == 0.0f &&
            StasisPath->CurrentVelocity.Length() == 0.0f,
        "Stasis zeroes movement speed while frozen");
    StasisStatus.RemoveEffect(Game::Components::StatusEffect::Stasis);
    StasisMovement.Update(0.1f);
    Require(StasisTrans->Position.X > 0.0f,
        "movement resumes along the preserved path after Stasis ends");

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

    Core::ECS::SystemManager OverlapEscapeSysMgr;
    auto& OverlapEscapeEM = OverlapEscapeSysMgr.GetEntityManager();
    Game::Systems::MovementSystem OverlapEscapeMovement(OverlapEscapeSysMgr);
    const int OverlapA = CreateMovableUnit(
        OverlapEscapeEM, {32.0f, 64.0f}, {256.0f, 64.0f},
        65, Game::Components::UnitSize::Small, 8);
    const int OverlapB = CreateMovableUnit(
        OverlapEscapeEM, {32.0f, 64.0f}, {256.0f, 64.0f},
        65, Game::Components::UnitSize::Small, 8);
    auto* OverlapAPath =
        OverlapEscapeEM.GetComponent<Game::Components::PathfindingComponent>(OverlapA);
    auto* OverlapBPath =
        OverlapEscapeEM.GetComponent<Game::Components::PathfindingComponent>(OverlapB);
    auto* OverlapATrans =
        OverlapEscapeEM.GetComponent<Game::Components::TransformComponent>(OverlapA);
    auto* OverlapBTrans =
        OverlapEscapeEM.GetComponent<Game::Components::TransformComponent>(OverlapB);
    Require(OverlapAPath && OverlapBPath && OverlapATrans && OverlapBTrans,
        "overlap escape test units have movement state");
    SetCollisionBounds(*OverlapAPath, 8, 8, 8, 8);
    SetCollisionBounds(*OverlapBPath, 8, 8, 8, 8);
    for (int I = 0; I < 80; ++I)
        OverlapEscapeMovement.Update(0.05f);
    Require(!CollisionBoundsOverlap(
                *OverlapATrans, *OverlapAPath, *OverlapBTrans, *OverlapBPath),
        "already-overlapped group units take separating steps instead of merging");
    Require(OverlapATrans->Position.X > 48.0f &&
            OverlapBTrans->Position.X > 48.0f,
        "overlapped group units both resume forward movement after separating");

    Core::ECS::SystemManager DynamicCollisionSysMgr;
    auto& DynamicCollisionEM = DynamicCollisionSysMgr.GetEntityManager();
    Game::Systems::MovementSystem DynamicCollisionMovement(DynamicCollisionSysMgr);
    int AsymmetricBlocker = CreateStationaryCollisionUnit(
        DynamicCollisionEM, {96.0f, 0.0f}, 40, 12, 4, 12);
    int LeftMover = CreateMovableUnit(
        DynamicCollisionEM, {0.0f, 0.0f}, {192.0f, 0.0f},
        0, Game::Components::UnitSize::Small, 8);
    auto* LeftMoverPath =
        DynamicCollisionEM.GetComponent<Game::Components::PathfindingComponent>(LeftMover);
    auto* BlockerPath =
        DynamicCollisionEM.GetComponent<Game::Components::PathfindingComponent>(AsymmetricBlocker);
    auto* LeftMoverTrans =
        DynamicCollisionEM.GetComponent<Game::Components::TransformComponent>(LeftMover);
    auto* BlockerTrans =
        DynamicCollisionEM.GetComponent<Game::Components::TransformComponent>(AsymmetricBlocker);
    Require(LeftMoverPath && BlockerPath && LeftMoverTrans && BlockerTrans,
        "dynamic collision coverage has transforms and DAT bounds");
    SetCollisionBounds(*LeftMoverPath, 8, 8, 8, 8);

    float LeftSideXAtSlide = -1.0f;
    for (int I = 0; I < 80; ++I)
    {
        const float PreviousY = LeftMoverTrans->Position.Y;
        DynamicCollisionMovement.Update(0.05f);
        Require(!CollisionBoundsOverlap(
                    *LeftMoverTrans, *LeftMoverPath, *BlockerTrans, *BlockerPath),
            "moving units never penetrate another unit's asymmetric collision bounds");
        if (LeftSideXAtSlide < 0.0f &&
            std::fabs(LeftMoverTrans->Position.Y - PreviousY) > 0.001f)
        {
            LeftSideXAtSlide = LeftMoverTrans->Position.X;
        }
    }
    Require(LeftSideXAtSlide >= 0.0f && LeftSideXAtSlide <= 48.1f,
        "approaching from the left respects the blocker's extended DAT left edge");

    Core::ECS::SystemManager RightCollisionSysMgr;
    auto& RightCollisionEM = RightCollisionSysMgr.GetEntityManager();
    Game::Systems::MovementSystem RightCollisionMovement(RightCollisionSysMgr);
    int RightBlocker = CreateStationaryCollisionUnit(
        RightCollisionEM, {96.0f, 0.0f}, 40, 12, 4, 12);
    int RightMover = CreateMovableUnit(
        RightCollisionEM, {192.0f, 0.0f}, {0.0f, 0.0f},
        0, Game::Components::UnitSize::Small, 8);
    auto* RightMoverPath =
        RightCollisionEM.GetComponent<Game::Components::PathfindingComponent>(RightMover);
    auto* RightBlockerPath =
        RightCollisionEM.GetComponent<Game::Components::PathfindingComponent>(RightBlocker);
    auto* RightMoverTrans =
        RightCollisionEM.GetComponent<Game::Components::TransformComponent>(RightMover);
    auto* RightBlockerTrans =
        RightCollisionEM.GetComponent<Game::Components::TransformComponent>(RightBlocker);
    Require(RightMoverPath && RightBlockerPath && RightMoverTrans && RightBlockerTrans,
        "right-side dynamic collision coverage has transforms and DAT bounds");
    SetCollisionBounds(*RightMoverPath, 8, 8, 8, 8);

    float RightSideXAtSlide = 999.0f;
    for (int I = 0; I < 80; ++I)
    {
        const float PreviousY = RightMoverTrans->Position.Y;
        RightCollisionMovement.Update(0.05f);
        Require(!CollisionBoundsOverlap(
                    *RightMoverTrans, *RightMoverPath,
                    *RightBlockerTrans, *RightBlockerPath),
            "right-side movement never penetrates asymmetric collision bounds");
        if (RightSideXAtSlide > 998.0f &&
            std::fabs(RightMoverTrans->Position.Y - PreviousY) > 0.001f)
        {
            RightSideXAtSlide = RightMoverTrans->Position.X;
        }
    }
    Require(RightSideXAtSlide < 999.0f && RightSideXAtSlide >= 108.9f,
        "approaching from the right uses the blocker's shorter DAT right edge");

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

    int DeadScv = CreateMovableUnit(
        ImmobileEM, {64.0f, 192.0f}, {256.0f, 192.0f},
        7,
        Game::Components::UnitSize::Small, 12);
    auto* DeadScvUnit = ImmobileEM.GetComponent<Game::Components::UnitComponent>(DeadScv);
    auto* DeadScvTrans = ImmobileEM.GetComponent<Game::Components::TransformComponent>(DeadScv);
    auto* DeadScvPath = ImmobileEM.GetComponent<Game::Components::PathfindingComponent>(DeadScv);
    Require(DeadScvUnit && DeadScvTrans && DeadScvPath,
        "dead movement coverage has unit, transform, and path data");
    DeadScvUnit->HitPoints = 0;
    ImmobileMovement.Update(0.2f);
    Require(DeadScvTrans->Position.X == 64.0f &&
            DeadScvTrans->Position.Y == 192.0f,
        "dead units do not move along stale paths before destruction cleanup");
    Require(DeadScvPath->Path.empty(),
        "dead units clear stale movement paths before destruction cleanup");

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

    // Reproduce the Meltdown mineral-row geometry: the exact expanded DAT
    // rectangle reaches y=116, while this 32-pixel path row is sampled at
    // y=112.  A center-only A* route therefore looks clear even though the
    // real segment hits the mineral.  Movement must use the reported static
    // blocker to request a genuine lateral detour.
    Game::Map::GameMap ExactStaticMap;
    ExactStaticMap.Generate(12, 8);
    Core::ECS::SystemManager ExactStaticSysMgr;
    auto& ExactStaticEM = ExactStaticSysMgr.GetEntityManager();
    Game::Systems::MovementSystem ExactStaticMovement(
        ExactStaticSysMgr, &ExactStaticMap);
    const int ExactStaticMover = CreateMovableUnit(
        ExactStaticEM, {171.0f, 123.0f}, {48.0f, 123.0f},
        64, Game::Components::UnitSize::Small, 11);
    auto* ExactStaticMoverPath =
        ExactStaticEM.GetComponent<Game::Components::PathfindingComponent>(
            ExactStaticMover);
    Require(ExactStaticMoverPath, "exact static detour mover has path state");
    SetCollisionBounds(*ExactStaticMoverPath, 11, 11, 11, 11);

    const int ExactMineral = ExactStaticEM.CreateEntity();
    auto& ExactMineralTransform =
        ExactStaticEM.AddComponent<Game::Components::TransformComponent>(ExactMineral);
    ExactMineralTransform.Position = {128.0f, 144.0f};
    auto& ExactMineralUnit =
        ExactStaticEM.AddComponent<Game::Components::UnitComponent>(ExactMineral);
    ExactMineralUnit.ChkUnitID = 176;
    ExactMineralUnit.Speed = 0.0f;
    ExactStaticEM.AddComponent<Game::Components::ResourceComponent>(ExactMineral).Type =
        Game::Components::ResourceType::Minerals;
    auto& ExactMineralPath =
        ExactStaticEM.AddComponent<Game::Components::PathfindingComponent>(ExactMineral);
    SetCollisionBounds(ExactMineralPath, 32, 16, 31, 15);

    float LargestExactDetour = 0.0f;
    for (int Step = 0; Step < 240; ++Step)
    {
        ExactStaticMovement.Update(0.05f);
        const auto* MoverTransform =
            ExactStaticEM.GetComponent<Game::Components::TransformComponent>(
                ExactStaticMover);
        Require(MoverTransform &&
                !CollisionBoundsOverlap(
                    *MoverTransform, *ExactStaticMoverPath,
                    ExactMineralTransform, ExactMineralPath),
            "strict static reroute never enters the mineral DAT footprint");
        LargestExactDetour = std::max(
            LargestExactDetour, std::fabs(MoverTransform->Position.Y - 123.0f));
    }
    const auto* ExactStaticMoverTransform =
        ExactStaticEM.GetComponent<Game::Components::TransformComponent>(ExactStaticMover);
    Require(ExactStaticMoverTransform && ExactStaticMoverTransform->Position.X < 72.0f,
        "unit crosses behind a static mineral instead of retrying the blocked straight segment");
    Require(LargestExactDetour > 16.0f,
        "static collision recovery contains a real map-backed lateral detour");

    // If strict padding seals the only corridor exit, movement must preserve
    // the exact blocker and mark the request failed instead of rebuilding the
    // known-colliding one-node route. HarvestSystem consumes this evidence on
    // its next update and can adopt the live blocking mineral.
    Game::Map::GameMap SealedStaticMap;
    SealedStaticMap.Generate(8, 5);
    for (int Y = 0; Y < SealedStaticMap.GetHeight(); ++Y)
    {
        for (int X = 0; X < SealedStaticMap.GetWidth(); ++X)
        {
            auto Tile = SealedStaticMap.GetTile(X, Y);
            Tile.Walkable = false;
            SealedStaticMap.SetTile(X, Y, Tile);
        }
    }
    for (int X = 1; X <= 5; ++X)
    {
        auto Tile = SealedStaticMap.GetTile(X, 3);
        Tile.Walkable = true;
        SealedStaticMap.SetTile(X, 3, Tile);
    }
    Core::ECS::SystemManager SealedStaticSysMgr;
    auto& SealedStaticEM = SealedStaticSysMgr.GetEntityManager();
    Game::Systems::MovementSystem SealedStaticMovement(
        SealedStaticSysMgr, &SealedStaticMap);
    const int SealedMover = CreateMovableUnit(
        SealedStaticEM, {171.0f, 123.0f}, {48.0f, 123.0f},
        64, Game::Components::UnitSize::Small, 11);
    auto* SealedMoverPath =
        SealedStaticEM.GetComponent<Game::Components::PathfindingComponent>(SealedMover);
    Require(SealedMoverPath, "sealed static mover has path state");
    SetCollisionBounds(*SealedMoverPath, 11, 11, 11, 11);
    auto* SealedMoverCommand =
        SealedStaticEM.GetComponent<Game::Components::CommandComponent>(SealedMover);
    Require(SealedMoverCommand, "sealed static mover has command state");
    SealedMoverCommand->CurrentCommand = Game::Components::CommandType::Harvest;

    const int SealingMineral = SealedStaticEM.CreateEntity();
    SealedStaticEM.AddComponent<Game::Components::TransformComponent>(SealingMineral).Position =
        {128.0f, 144.0f};
    auto& SealingMineralUnit =
        SealedStaticEM.AddComponent<Game::Components::UnitComponent>(SealingMineral);
    SealingMineralUnit.ChkUnitID = 178;
    SealedStaticEM.AddComponent<Game::Components::ResourceComponent>(SealingMineral).Type =
        Game::Components::ResourceType::Minerals;
    auto& SealingMineralPath =
        SealedStaticEM.AddComponent<Game::Components::PathfindingComponent>(SealingMineral);
    SetCollisionBounds(SealingMineralPath, 32, 16, 31, 15);

    const int SealedTargetMineral = SealedStaticEM.CreateEntity();
    SealedStaticEM.AddComponent<Game::Components::TransformComponent>(
        SealedTargetMineral).Position = {48.0f, 144.0f};
    auto& SealedTargetUnit =
        SealedStaticEM.AddComponent<Game::Components::UnitComponent>(SealedTargetMineral);
    SealedTargetUnit.ChkUnitID = 176;
    SealedStaticEM.AddComponent<Game::Components::ResourceComponent>(
        SealedTargetMineral).Type = Game::Components::ResourceType::Minerals;
    auto& SealedTargetPath =
        SealedStaticEM.AddComponent<Game::Components::PathfindingComponent>(
            SealedTargetMineral);
    SetCollisionBounds(SealedTargetPath, 32, 16, 31, 15);
    auto& SealedHarvest =
        SealedStaticEM.AddComponent<Game::Components::HarvestComponent>(SealedMover);
    SealedHarvest.State = Game::Components::HarvestState::MovingToResource;
    SealedHarvest.TargetResourceID = SealedTargetMineral;
    SealedHarvest.DesiredResourceID = SealedTargetMineral;
    SealedMoverPath->RequestedIgnoreEntityID = SealedTargetMineral;

    for (int Step = 0; Step < 120 && !SealedMoverPath->HasRecentMoveFailure; ++Step)
        SealedStaticMovement.Update(0.05f);
    Require(SealedMoverPath->Path.empty() &&
            SealedMoverPath->HasRecentMoveFailure &&
            SealedMoverPath->BlockedByEntityID == SealingMineral &&
            SealedMoverPath->RequestedIgnoreEntityID == SealedTargetMineral,
        "strict no-route movement preserves mineral blocker evidence without fallback cycling");

    Core::ECS::SystemManager FriendlyBaseSysMgr;
    auto& FriendlyBaseEM = FriendlyBaseSysMgr.GetEntityManager();
    Game::Map::GameMap FriendlyBaseMap;
    FriendlyBaseMap.Generate(16, 8);
    Game::Systems::MovementSystem FriendlyBaseMovement(
        FriendlyBaseSysMgr, &FriendlyBaseMap);
    const int BaseMover = CreateMovableUnit(
        FriendlyBaseEM, {260.0f, 111.0f}, {80.0f, 111.0f},
        0, Game::Components::UnitSize::Small, 8);
    auto* BaseMoverPath =
        FriendlyBaseEM.GetComponent<Game::Components::PathfindingComponent>(BaseMover);
    Require(BaseMoverPath, "friendly-base mover has path state");
    SetCollisionBounds(*BaseMoverPath, 8, 9, 8, 10);
    FriendlyBaseEM.AddComponent<Game::Components::PlayerComponent>(BaseMover).PlayerID = 0;

    const int FriendlyBuilding = FriendlyBaseEM.CreateEntity();
    auto& FriendlyBuildingTransform =
        FriendlyBaseEM.AddComponent<Game::Components::TransformComponent>(FriendlyBuilding);
    FriendlyBuildingTransform.Position = {192.0f, 128.0f};
    auto& FriendlyBuildingUnit =
        FriendlyBaseEM.AddComponent<Game::Components::UnitComponent>(FriendlyBuilding);
    FriendlyBuildingUnit.ChkUnitID = 111;
    FriendlyBuildingUnit.Speed = 0.0f;
    auto& FriendlyBuildingState =
        FriendlyBaseEM.AddComponent<Game::Components::BuildingComponent>(FriendlyBuilding);
    FriendlyBuildingState.IsBuilding = true;
    FriendlyBuildingState.IsLifted = false;
    auto& FriendlyBuildingPath =
        FriendlyBaseEM.AddComponent<Game::Components::PathfindingComponent>(FriendlyBuilding);
    SetCollisionBounds(FriendlyBuildingPath, 48, 32, 56, 32);
    FriendlyBaseEM.AddComponent<Game::Components::PlayerComponent>(FriendlyBuilding).PlayerID = 0;

    float BaseDetour = 0.0f;
    for (int Step = 0; Step < 320; ++Step)
    {
        FriendlyBaseMovement.Update(0.05f);
        const auto* MoverTransform =
            FriendlyBaseEM.GetComponent<Game::Components::TransformComponent>(BaseMover);
        Require(MoverTransform &&
                !CollisionBoundsOverlap(
                    *MoverTransform, *BaseMoverPath,
                    FriendlyBuildingTransform, FriendlyBuildingPath),
            "ordinary allied unit never passes through a friendly building footprint");
        BaseDetour = std::max(
            BaseDetour, std::fabs(MoverTransform->Position.Y - 111.0f));
    }
    const auto* BaseMoverTransform =
        FriendlyBaseEM.GetComponent<Game::Components::TransformComponent>(BaseMover);
    Require(BaseMoverTransform && BaseMoverTransform->Position.X < 112.0f,
        "ordinary allied unit reaches the far side of its own base building");
    Require(BaseDetour > 20.0f,
        "friendly-base traversal uses a lateral building detour");

    Core::ECS::SystemManager EmbeddedUnitSysMgr;
    auto& EmbeddedUnitEM = EmbeddedUnitSysMgr.GetEntityManager();
    Game::Systems::MovementSystem EmbeddedUnitMovement(EmbeddedUnitSysMgr);
    const int EmbeddedMover = CreateMovableUnit(
        EmbeddedUnitEM, {48.0f, 96.0f}, {256.0f, 96.0f},
        65, Game::Components::UnitSize::Small, 8);
    auto* EmbeddedMoverPath =
        EmbeddedUnitEM.GetComponent<Game::Components::PathfindingComponent>(EmbeddedMover);
    Require(EmbeddedMoverPath, "embedded ground unit has movement bounds");
    SetCollisionBounds(*EmbeddedMoverPath, 8, 9, 8, 10);
    EmbeddedUnitEM.AddComponent<Game::Components::PlayerComponent>(EmbeddedMover).PlayerID = 2;

    const int EmbeddedBuilding = EmbeddedUnitEM.CreateEntity();
    EmbeddedUnitEM.AddComponent<Game::Components::TransformComponent>(EmbeddedBuilding).Position =
        {96.0f, 96.0f};
    auto& EmbeddedBuildingUnit =
        EmbeddedUnitEM.AddComponent<Game::Components::UnitComponent>(EmbeddedBuilding);
    EmbeddedBuildingUnit.ChkUnitID = 160; // Protoss Gateway.
    EmbeddedBuildingUnit.Speed = 0.0f;
    auto& EmbeddedBuildingState =
        EmbeddedUnitEM.AddComponent<Game::Components::BuildingComponent>(EmbeddedBuilding);
    EmbeddedBuildingState.IsBuilding = true;
    EmbeddedBuildingState.IsLifted = false;
    auto& EmbeddedBuildingPath =
        EmbeddedUnitEM.AddComponent<Game::Components::PathfindingComponent>(EmbeddedBuilding);
    SetCollisionBounds(EmbeddedBuildingPath, 48, 40, 56, 32);
    EmbeddedUnitEM.AddComponent<Game::Components::PlayerComponent>(EmbeddedBuilding).PlayerID = 2;

    EmbeddedUnitMovement.Update(0.05f);
    const auto* EmbeddedMoverTrans =
        EmbeddedUnitEM.GetComponent<Game::Components::TransformComponent>(EmbeddedMover);
    Require(EmbeddedMoverTrans && EmbeddedMoverTrans->Position.X <= 39.1f,
        "a non-harvest unit spawned inside a building takes the shortest escape even when it is backward");

    Core::ECS::SystemManager TargetBuildingSysMgr;
    auto& TargetBuildingEM = TargetBuildingSysMgr.GetEntityManager();
    Game::Systems::MovementSystem TargetBuildingMovement(TargetBuildingSysMgr);
    const int TargetBuildingMover = CreateMovableUnit(
        TargetBuildingEM, {0.0f, 288.0f}, {96.0f, 288.0f},
        0, Game::Components::UnitSize::Small, 8);
    auto* TargetBuildingMoverPath =
        TargetBuildingEM.GetComponent<Game::Components::PathfindingComponent>(
            TargetBuildingMover);
    SetCollisionBounds(*TargetBuildingMoverPath, 8, 9, 8, 10);
    TargetBuildingEM.AddComponent<Game::Components::PlayerComponent>(
        TargetBuildingMover).PlayerID = 0;

    const int TargetBuilding = TargetBuildingEM.CreateEntity();
    auto& TargetBuildingTransform =
        TargetBuildingEM.AddComponent<Game::Components::TransformComponent>(TargetBuilding);
    TargetBuildingTransform.Position = {96.0f, 288.0f};
    auto& TargetBuildingUnit =
        TargetBuildingEM.AddComponent<Game::Components::UnitComponent>(TargetBuilding);
    TargetBuildingUnit.ChkUnitID = 111;
    TargetBuildingUnit.Speed = 0.0f;
    auto& TargetBuildingState =
        TargetBuildingEM.AddComponent<Game::Components::BuildingComponent>(TargetBuilding);
    TargetBuildingState.IsBuilding = true;
    TargetBuildingState.IsLifted = false;
    auto& TargetBuildingPath =
        TargetBuildingEM.AddComponent<Game::Components::PathfindingComponent>(TargetBuilding);
    SetCollisionBounds(TargetBuildingPath, 48, 40, 56, 32);
    TargetBuildingEM.AddComponent<Game::Components::PlayerComponent>(TargetBuilding).PlayerID = 1;

    auto* TargetBuildingCommand =
        TargetBuildingEM.GetComponent<Game::Components::CommandComponent>(TargetBuildingMover);
    TargetBuildingCommand->CurrentCommand = Game::Components::CommandType::Attack;
    TargetBuildingCommand->TargetEntityID = TargetBuilding;
    TargetBuildingMoverPath->RequestedTargetEntityID = TargetBuilding;
    TargetBuildingMoverPath->RequestedIgnoreEntityID = TargetBuilding;
    for (int I = 0; I < 80; ++I)
    {
        TargetBuildingMovement.Update(0.05f);
        const auto* MoverTransform =
            TargetBuildingEM.GetComponent<Game::Components::TransformComponent>(
                TargetBuildingMover);
        Require(MoverTransform &&
                !CollisionBoundsOverlap(
                    *MoverTransform, *TargetBuildingMoverPath,
                    TargetBuildingTransform, TargetBuildingPath),
            "a planning-only target ignore never permits runtime building overlap");
    }
    const auto* TargetBuildingMoverTransform =
        TargetBuildingEM.GetComponent<Game::Components::TransformComponent>(
            TargetBuildingMover);
    Require(TargetBuildingMoverTransform &&
            TargetBuildingMoverTransform->Position.X <= 39.1f,
        "a unit approaches an ignored target building at its footprint edge instead of passing through");

    Core::ECS::SystemManager RallyEscapeSysMgr;
    auto& RallyEscapeEM = RallyEscapeSysMgr.GetEntityManager();
    Game::Systems::MovementSystem RallyEscapeMovement(RallyEscapeSysMgr);
    const int RallyBuilding = RallyEscapeEM.CreateEntity();
    auto& RallyBuildingTransform =
        RallyEscapeEM.AddComponent<Game::Components::TransformComponent>(RallyBuilding);
    RallyBuildingTransform.Position = {96.0f, 448.0f};
    auto& RallyBuildingUnit =
        RallyEscapeEM.AddComponent<Game::Components::UnitComponent>(RallyBuilding);
    RallyBuildingUnit.ChkUnitID = 160; // Allied Protoss Gateway producer.
    RallyBuildingUnit.Speed = 0.0f;
    auto& RallyBuildingState =
        RallyEscapeEM.AddComponent<Game::Components::BuildingComponent>(RallyBuilding);
    RallyBuildingState.IsBuilding = true;
    RallyBuildingState.IsLifted = false;
    auto& RallyBuildingPath =
        RallyEscapeEM.AddComponent<Game::Components::PathfindingComponent>(RallyBuilding);
    SetCollisionBounds(RallyBuildingPath, 48, 40, 56, 32);
    RallyEscapeEM.AddComponent<Game::Components::PlayerComponent>(RallyBuilding).PlayerID = 3;

    const std::vector<Core::Math::Vec2> RallyStarts{
        {48.0f, 430.0f}, {60.0f, 440.0f},
        {48.0f, 452.0f}, {60.0f, 462.0f}};
    const std::vector<Core::Math::Vec2> RallyTargets{
        {-96.0f, 412.0f}, {-96.0f, 436.0f},
        {-96.0f, 460.0f}, {-96.0f, 484.0f}};
    std::vector<int> RallyUnits;
    std::vector<float> RallyBuildingOverlap;
    for (size_t I = 0; I < RallyStarts.size(); ++I)
    {
        const int RallyUnitID = CreateMovableUnit(
            RallyEscapeEM, RallyStarts[I], RallyTargets[I],
            65, Game::Components::UnitSize::Small, 8);
        auto* UnitPath =
            RallyEscapeEM.GetComponent<Game::Components::PathfindingComponent>(RallyUnitID);
        SetCollisionBounds(*UnitPath, 8, 9, 8, 10);
        RallyEscapeEM.AddComponent<Game::Components::PlayerComponent>(RallyUnitID).PlayerID = 3;
        const auto* UnitTransform =
            RallyEscapeEM.GetComponent<Game::Components::TransformComponent>(RallyUnitID);
        RallyUnits.push_back(RallyUnitID);
        RallyBuildingOverlap.push_back(CollisionBoundsOverlapArea(
            *UnitTransform, *UnitPath, RallyBuildingTransform, RallyBuildingPath));
        Require(RallyBuildingOverlap.back() > 0.0f,
            "rally escape coverage starts each allied unit inside its producer footprint");
    }

    for (int Step = 0; Step < 180; ++Step)
    {
        RallyEscapeMovement.Update(0.05f);
        for (size_t I = 0; I < RallyUnits.size(); ++I)
        {
            const auto* UnitTransform =
                RallyEscapeEM.GetComponent<Game::Components::TransformComponent>(RallyUnits[I]);
            const auto* UnitPath =
                RallyEscapeEM.GetComponent<Game::Components::PathfindingComponent>(RallyUnits[I]);
            const float Overlap = CollisionBoundsOverlapArea(
                *UnitTransform, *UnitPath, RallyBuildingTransform, RallyBuildingPath);
            Require(Overlap <= RallyBuildingOverlap[I] + 0.01f,
                "rally units only reduce an existing producer overlap while escaping");
            RallyBuildingOverlap[I] = Overlap;
        }
    }
    for (size_t I = 0; I < RallyUnits.size(); ++I)
    {
        const auto* UnitTransform =
            RallyEscapeEM.GetComponent<Game::Components::TransformComponent>(RallyUnits[I]);
        Require(UnitTransform && UnitTransform->Position.X < -48.0f &&
                RallyBuildingOverlap[I] == 0.0f,
            "a crowded allied production group exits the footprint instead of permanently blocking");
        for (size_t J = I + 1; J < RallyUnits.size(); ++J)
        {
            const auto* OtherTransform =
                RallyEscapeEM.GetComponent<Game::Components::TransformComponent>(RallyUnits[J]);
            Require(OtherTransform &&
                    UnitTransform->Position.DistanceTo(OtherTransform->Position) > 4.0f,
                "crowded rally units do not collapse into one coordinate while escaping");
        }
    }

    Core::ECS::SystemManager IgnoredTargetSysMgr;
    auto& IgnoredTargetEM = IgnoredTargetSysMgr.GetEntityManager();
    Game::Systems::MovementSystem IgnoredTargetMovement(IgnoredTargetSysMgr);
    const int HarvestMover = CreateMovableUnit(
        IgnoredTargetEM, {0.0f, 0.0f}, {128.0f, 0.0f},
        7, Game::Components::UnitSize::Small, 12);
    const int HarvestTarget = IgnoredTargetEM.CreateEntity();
    IgnoredTargetEM.AddComponent<Game::Components::TransformComponent>(HarvestTarget).Position =
        {64.0f, 0.0f};
    auto& HarvestTargetUnit =
        IgnoredTargetEM.AddComponent<Game::Components::UnitComponent>(HarvestTarget);
    HarvestTargetUnit.ChkUnitID = 176;
    IgnoredTargetEM.AddComponent<Game::Components::ResourceComponent>(HarvestTarget).Type =
        Game::Components::ResourceType::Minerals;
    auto& HarvestTargetPath =
        IgnoredTargetEM.AddComponent<Game::Components::PathfindingComponent>(HarvestTarget);
    SetCollisionBounds(HarvestTargetPath, 32, 20, 31, 19);
    auto* HarvestMoverPath =
        IgnoredTargetEM.GetComponent<Game::Components::PathfindingComponent>(HarvestMover);
    auto* HarvestMoverTrans =
        IgnoredTargetEM.GetComponent<Game::Components::TransformComponent>(HarvestMover);
    Require(HarvestMoverPath && HarvestMoverTrans,
        "harvest approach mover has pathing and transform state");
    auto& HarvestMoverState =
        IgnoredTargetEM.AddComponent<Game::Components::HarvestComponent>(HarvestMover);
    HarvestMoverState.State = Game::Components::HarvestState::MovingToResource;
    HarvestMoverState.TargetResourceID = HarvestTarget;
    HarvestMoverPath->RequestedIgnoreEntityID = HarvestTarget;
    for (int I = 0; I < 45; ++I)
        IgnoredTargetMovement.Update(0.05f);
    Require(HarvestMoverTrans->Position.X > 96.0f,
        "movement honors the command target ignore id instead of sticking at a resource boundary");

    Core::ECS::SystemManager ConstructionLaneSysMgr;
    auto& ConstructionLaneEM = ConstructionLaneSysMgr.GetEntityManager();
    Game::Systems::MovementSystem ConstructionLaneMovement(ConstructionLaneSysMgr);
    const int ConstructionMover = CreateMovableUnit(
        ConstructionLaneEM, {0.0f, 32.0f}, {160.0f, 32.0f},
        7, Game::Components::UnitSize::Small, 10);
    CreateStationaryCollisionUnit(
        ConstructionLaneEM, {64.0f, 32.0f}, 12, 12, 12, 12);
    const int ConstructionTarget = ConstructionLaneEM.CreateEntity();
    auto& ConstructionBuilding =
        ConstructionLaneEM.AddComponent<Game::Components::BuildingComponent>(ConstructionTarget);
    ConstructionBuilding.UnderConstruction = true;
    ConstructionBuilding.BuilderUnitID = ConstructionMover;
    auto* ConstructionMoverPath =
        ConstructionLaneEM.GetComponent<Game::Components::PathfindingComponent>(ConstructionMover);
    auto* ConstructionMoverTrans =
        ConstructionLaneEM.GetComponent<Game::Components::TransformComponent>(ConstructionMover);
    Require(ConstructionMoverPath && ConstructionMoverTrans,
        "construction lane worker has movement state");
    ConstructionMoverPath->RequestedIgnoreEntityID = ConstructionTarget;
    for (int I = 0; I < 45; ++I)
        ConstructionLaneMovement.Update(0.05f);
    Require(ConstructionMoverTrans->Position.X > 96.0f,
        "assigned construction SCV crosses crowded worker lanes instead of deadlocking");

    Core::ECS::SystemManager HarvestLaneSysMgr;
    auto& HarvestLaneEM = HarvestLaneSysMgr.GetEntityManager();
    Game::Systems::MovementSystem HarvestLaneMovement(HarvestLaneSysMgr);
    const int InboundHarvester = CreateMovableUnit(
        HarvestLaneEM, {32.0f, 32.0f}, {192.0f, 32.0f},
        7, Game::Components::UnitSize::Small, 10);
    const int ReturningHarvester = CreateMovableUnit(
        HarvestLaneEM, {96.0f, 32.0f}, {0.0f, 32.0f},
        7, Game::Components::UnitSize::Small, 10);
    auto& InboundHarvest =
        HarvestLaneEM.AddComponent<Game::Components::HarvestComponent>(InboundHarvester);
    InboundHarvest.State = Game::Components::HarvestState::MovingToResource;
    auto& ReturningHarvest =
        HarvestLaneEM.AddComponent<Game::Components::HarvestComponent>(ReturningHarvester);
    ReturningHarvest.State = Game::Components::HarvestState::MovingToDepot;
    for (int I = 0; I < 60; ++I)
        HarvestLaneMovement.Update(0.05f);
    const auto* InboundHarvesterTrans =
        HarvestLaneEM.GetComponent<Game::Components::TransformComponent>(InboundHarvester);
    const auto* ReturningHarvesterTrans =
        HarvestLaneEM.GetComponent<Game::Components::TransformComponent>(ReturningHarvester);
    Require(InboundHarvesterTrans && ReturningHarvesterTrans &&
            InboundHarvesterTrans->Position.X > 112.0f &&
            ReturningHarvesterTrans->Position.X < 24.0f,
        "inbound and returning harvesters use OpenBW-style no-collide lanes instead of deadlocking");

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
        CorridorEM, {48.0f, 16.0f}, {112.0f, 16.0f},
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

    Core::ECS::SystemManager InfantryGapSysMgr;
    auto& InfantryGapEM = InfantryGapSysMgr.GetEntityManager();
    Game::Systems::MovementSystem InfantryGapMovement(InfantryGapSysMgr);
    const int GapMarine = CreateMovableUnit(
        InfantryGapEM, {64.0f, 0.0f}, {64.0f, 192.0f},
        0, Game::Components::UnitSize::Small, 10);
    auto* GapMarinePath =
        InfantryGapEM.GetComponent<Game::Components::PathfindingComponent>(GapMarine);
    Require(GapMarinePath, "infantry gap test has movement bounds");
    SetCollisionBounds(*GapMarinePath, 8, 9, 8, 10);

    const int LeftGapBlocker = InfantryGapEM.CreateEntity();
    InfantryGapEM.AddComponent<Game::Components::TransformComponent>(LeftGapBlocker).Position =
        {20.0f, 96.0f};
    auto& LeftGapUnit = InfantryGapEM.AddComponent<Game::Components::UnitComponent>(LeftGapBlocker);
    LeftGapUnit.ChkUnitID = 125;
    InfantryGapEM.AddComponent<Game::Components::BuildingComponent>(LeftGapBlocker).IsBuilding = true;
    auto& LeftGapPath = InfantryGapEM.AddComponent<Game::Components::PathfindingComponent>(LeftGapBlocker);
    SetCollisionBounds(LeftGapPath, 20, 96, 34, 96);

    const int RightGapBlocker = InfantryGapEM.CreateEntity();
    InfantryGapEM.AddComponent<Game::Components::TransformComponent>(RightGapBlocker).Position =
        {100.0f, 96.0f};
    auto& RightGapUnit = InfantryGapEM.AddComponent<Game::Components::UnitComponent>(RightGapBlocker);
    RightGapUnit.ChkUnitID = 30;
    InfantryGapEM.AddComponent<Game::Components::BuildingComponent>(RightGapBlocker).IsBuilding = true;
    auto& RightGapPath = InfantryGapEM.AddComponent<Game::Components::PathfindingComponent>(RightGapBlocker);
    SetCollisionBounds(RightGapPath, 27, 96, 20, 96);

    for (int I = 0; I < 80; ++I)
        InfantryGapMovement.Update(0.05f);
    const auto* GapMarineTrans =
        InfantryGapEM.GetComponent<Game::Components::TransformComponent>(GapMarine);
    Require(GapMarineTrans && GapMarineTrans->Position.Y > 150.0f,
        "Marine uses directional units.dat bounds to pass an infantry-width bunker/tank gap");

    Core::ECS::SystemManager TeamQueueSysMgr;
    auto& TeamQueueEM = TeamQueueSysMgr.GetEntityManager();
    Game::Systems::MovementSystem TeamQueueMovement(TeamQueueSysMgr);
    const int SlowFront = CreateMovableUnit(
        TeamQueueEM, {48.0f, 32.0f}, {288.0f, 32.0f},
        0, Game::Components::UnitSize::Small, 8);
    const int FastRear = CreateMovableUnit(
        TeamQueueEM, {16.0f, 32.0f}, {256.0f, 32.0f},
        0, Game::Components::UnitSize::Small, 8);
    auto* SlowFrontPath =
        TeamQueueEM.GetComponent<Game::Components::PathfindingComponent>(SlowFront);
    auto* FastRearPath =
        TeamQueueEM.GetComponent<Game::Components::PathfindingComponent>(FastRear);
    Require(SlowFrontPath && FastRearPath, "team queue test units have movement state");
    SetCollisionBounds(*SlowFrontPath, 8, 9, 8, 10);
    SetCollisionBounds(*FastRearPath, 8, 9, 8, 10);
    SlowFrontPath->TargetSpeed = 24.0f;
    FastRearPath->TargetSpeed = 64.0f;
    for (int I = 0; I < 240; ++I)
        TeamQueueMovement.Update(0.05f);
    const auto* FastRearTrans =
        TeamQueueEM.GetComponent<Game::Components::TransformComponent>(FastRear);
    Require(FastRearTrans && FastRearTrans->Position.X > 220.0f,
        "rear group unit waits for a moving teammate and continues after the lane clears");
    Require(!FastRearPath->HasRecentMoveFailure,
        "temporary teammate blocking does not blacklist a reachable group destination");

    Core::ECS::SystemManager MovingYieldSysMgr;
    auto& MovingYieldEM = MovingYieldSysMgr.GetEntityManager();
    Game::Systems::MovementSystem MovingYieldMovement(MovingYieldSysMgr);
    const int MovingYieldUnit = CreateMovableUnit(
        MovingYieldEM, {32.0f, 32.0f}, {192.0f, 32.0f},
        0, Game::Components::UnitSize::Small, 8);
    auto* MovingYieldPath =
        MovingYieldEM.GetComponent<Game::Components::PathfindingComponent>(MovingYieldUnit);
    auto* MovingYieldTrans =
        MovingYieldEM.GetComponent<Game::Components::TransformComponent>(MovingYieldUnit);
    Require(MovingYieldPath && MovingYieldTrans,
        "moving friendly-yield test has movement state");
    MovingYieldPath->CurrentVelocity = {20.0f, 0.0f};
    MovingYieldPath->FriendlyYieldVector = {0.0f, 1.0f};
    MovingYieldMovement.Update(0.05f);
    Require(std::fabs(MovingYieldTrans->Position.Y - 32.0f) < 0.1f,
        "an actively moving unit does not get pushed off its route by a yield request");

    Core::ECS::SystemManager FriendlyYieldSysMgr;
    auto& FriendlyYieldEM = FriendlyYieldSysMgr.GetEntityManager();
    Game::Systems::MovementSystem FriendlyYieldMovement(FriendlyYieldSysMgr);
    const int YieldingFriend = CreateStationaryCollisionUnit(
        FriendlyYieldEM, {80.0f, 64.0f}, 8, 9, 8, 10);
    const int YieldingMover = CreateMovableUnit(
        FriendlyYieldEM, {48.0f, 64.0f}, {192.0f, 64.0f},
        0, Game::Components::UnitSize::Small, 8);
    FriendlyYieldEM.AddComponent<Game::Components::PlayerComponent>(YieldingFriend).PlayerID = 0;
    FriendlyYieldEM.AddComponent<Game::Components::PlayerComponent>(YieldingMover).PlayerID = 0;
    auto* YieldingMoverPath =
        FriendlyYieldEM.GetComponent<Game::Components::PathfindingComponent>(YieldingMover);
    auto* YieldingFriendTrans =
        FriendlyYieldEM.GetComponent<Game::Components::TransformComponent>(YieldingFriend);
    auto* YieldingMoverTrans =
        FriendlyYieldEM.GetComponent<Game::Components::TransformComponent>(YieldingMover);
    Require(YieldingMoverPath && YieldingFriendTrans && YieldingMoverTrans,
        "friendly yielding test has movement and transform state");
    SetCollisionBounds(*YieldingMoverPath, 8, 9, 8, 10);
    const Core::Math::Vec2 FriendStart = YieldingFriendTrans->Position;
    for (int I = 0; I < 120; ++I)
        FriendlyYieldMovement.Update(0.05f);
    Require(YieldingFriendTrans->Position.DistanceTo(FriendStart) > 1.0f,
        "a stopped friendly unit yields a small amount to an approaching teammate");
    Require(YieldingMoverTrans->Position.X > 120.0f,
        "the approaching teammate continues after the friendly unit yields");
    Require(YieldingFriendTrans->Position.DistanceTo(FriendStart) <= 24.1f,
        "friendly yielding stays bounded instead of pushing the blocker away indefinitely");

    Core::ECS::SystemManager FriendlyDetourSysMgr;
    auto& FriendlyDetourEM = FriendlyDetourSysMgr.GetEntityManager();
    Game::Map::GameMap FriendlyDetourMap;
    FriendlyDetourMap.Generate(12, 6);
    Game::Systems::MovementSystem FriendlyDetourMovement(
        FriendlyDetourSysMgr, &FriendlyDetourMap);
    const int HoldingFriend = CreateStationaryCollisionUnit(
        FriendlyDetourEM, {112.0f, 80.0f}, 8, 9, 8, 10);
    auto* HoldingCommand =
        FriendlyDetourEM.GetComponent<Game::Components::CommandComponent>(HoldingFriend);
    Require(HoldingCommand, "friendly detour blocker has a command component");
    HoldingCommand->CurrentCommand = Game::Components::CommandType::HoldPosition;
    const int DetourMover = CreateMovableUnit(
        FriendlyDetourEM, {48.0f, 80.0f}, {304.0f, 80.0f},
        0, Game::Components::UnitSize::Small, 8);
    FriendlyDetourEM.AddComponent<Game::Components::PlayerComponent>(HoldingFriend).PlayerID = 0;
    FriendlyDetourEM.AddComponent<Game::Components::PlayerComponent>(DetourMover).PlayerID = 0;
    auto* DetourMoverPath =
        FriendlyDetourEM.GetComponent<Game::Components::PathfindingComponent>(DetourMover);
    auto* DetourMoverTrans =
        FriendlyDetourEM.GetComponent<Game::Components::TransformComponent>(DetourMover);
    Require(DetourMoverPath && DetourMoverTrans,
        "friendly detour mover has path and transform state");
    SetCollisionBounds(*DetourMoverPath, 8, 9, 8, 10);
    float MaxDetourY = DetourMoverTrans->Position.Y;
    for (int I = 0; I < 160; ++I)
    {
        FriendlyDetourMovement.Update(0.05f);
        MaxDetourY = std::max(MaxDetourY, DetourMoverTrans->Position.Y);
    }
    Require(DetourMoverTrans->Position.X > 240.0f,
        "unit reroutes around a friendly blocker when yielding cannot clear the lane");
    Require(MaxDetourY > 96.0f,
        "friendly-blocker reroute contains a real lateral A* detour");

    Core::ECS::SystemManager RetrySysMgr;
    auto& RetryEM = RetrySysMgr.GetEntityManager();
    Game::Systems::MovementSystem RetryMovement(RetrySysMgr);
    int RetryUnit = CreateMovableUnit(
        RetryEM, {0.0f, 0.0f}, {128.0f, 0.0f},
        0, Game::Components::UnitSize::Small, 8);
    auto* RetryPath =
        RetryEM.GetComponent<Game::Components::PathfindingComponent>(RetryUnit);
    auto* RetryTrans =
        RetryEM.GetComponent<Game::Components::TransformComponent>(RetryUnit);
    Require(RetryPath && RetryTrans, "failed-target retry unit has movement state");
    RetryPath->Path.clear();
    RetryPath->HasRecentMoveFailure = true;
    RetryPath->RecentFailedMoveTarget = RetryPath->RequestedDestination;
    RetryPath->RecentMoveFailureCooldown = 1.0f;
    for (int I = 0; I < 9; ++I)
        RetryMovement.Update(0.1f);
    Require(RetryPath->HasRecentMoveFailure && RetryPath->Path.empty(),
        "failed movement target remains backed off during the retry cooldown");
    for (int I = 0; I < 3; ++I)
        RetryMovement.Update(0.1f);
    Require(!RetryPath->HasRecentMoveFailure,
        "failed movement target expires instead of remaining permanently blacklisted");
    Require(!RetryPath->Path.empty() || RetryTrans->Position.X > 0.0f,
        "movement automatically retries the same target after the cooldown");

    std::printf("MovementCommandTest passed\n");
    return 0;
}
