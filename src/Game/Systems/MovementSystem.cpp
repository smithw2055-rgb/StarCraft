#include "Game/Systems/MovementSystem.h"
#include "Game/Systems/CommandSystem.h"
#include "Game/Systems/UpgradeSystem.h"
#include "Game/MovementTuning.h"
#include "Game/Components.h"
#include "Game/DataEnums.h"
#include "Game/Map/Map.h"
#include "Game/Pathfinding/AStar.h"
#include "Core/Math/Vec2.h"

#include <unordered_map>
#include <cmath>
#include <algorithm>
#include <functional>
#include <vector>

namespace Game::Systems
{

using namespace Game::Components;

namespace
{
    static bool IsSiegedTankUnit(int ChkUnitID)
    {
        return ChkUnitID == 30 ||  // Terran Siege Tank Siege Mode
               ChkUnitID == 25;    // Hero Edmund Duke Siege Mode
    }

    static bool CanMoveEntity(Core::ECS::EntityManager& EM, Core::ECS::EntityID ID)
    {
        auto* Trans = EM.GetComponent<TransformComponent>(ID);
        auto* Unit = EM.GetComponent<UnitComponent>(ID);
        if (!Trans || !Unit)
            return false;
        if (Unit->HitPoints <= 0)
            return false;
        if (EM.GetComponent<ResourceComponent>(ID))
            return false;
        if (auto* Building = EM.GetComponent<BuildingComponent>(ID))
            return Building->IsLifted &&
                   Building->FlightPhase == BuildingFlightPhase::Airborne;
        if (IsSiegedTankUnit(Unit->ChkUnitID))
            return false;
        return Unit->Speed > 0.0f;
    }

    static bool ShouldMoveAsAir(Core::ECS::EntityManager& EM, Core::ECS::EntityID ID)
    {
        if (auto* Building = EM.GetComponent<BuildingComponent>(ID))
            if (Building->IsLifted)
                return true;

        auto* Unit = EM.GetComponent<UnitComponent>(ID);
        return Unit && Game::Data::IsAirUnitType(Unit->ChkUnitID);
    }

    static bool IsStaticGroundBlocker(Core::ECS::EntityManager& EM, Core::ECS::EntityID ID)
    {
        if (auto* Building = EM.GetComponent<BuildingComponent>(ID))
            return !Building->IsLifted ||
                   Building->FlightPhase == BuildingFlightPhase::Landing;
        return EM.GetComponent<ResourceComponent>(ID) != nullptr;
    }

    struct StaticBlockerRect
    {
        float Left = 16.0f;
        float Top = 16.0f;
        float Right = 16.0f;
        float Bottom = 16.0f;
        Core::Math::Vec2 Position;
    };

    static int CollisionRadiusFromBounds(const PathfindingComponent& Path)
    {
        if (!Path.HasCollisionBounds)
            return Path.CollisionRadius;

        const int Width = Path.CollisionLeft + Path.CollisionRight + 1;
        const int Height = Path.CollisionUp + Path.CollisionDown + 1;
        return std::max(4, std::max(Width, Height) / 2);
    }

    static StaticBlockerRect StaticBlockerRectForEntity(const UnitComponent* Unit,
                                                        const BuildingComponent* Building,
                                                        const ResourceComponent* Resource,
                                                        const PathfindingComponent* Path)
    {
        if (Path && Path->HasCollisionBounds)
        {
            return {
                (float)Path->CollisionLeft,
                (float)Path->CollisionUp,
                (float)Path->CollisionRight + 1.0f,
                (float)Path->CollisionDown + 1.0f
            };
        }

        Core::Math::Vec2 Half{16.0f, 16.0f};
        if (Resource)
        {
            Half = {32.0f, 20.0f};
        }
        else if (Building && Unit)
        {
            switch (Unit->ChkUnitID)
            {
                case 106: // Terran Command Center
                    Half = {64.0f, 48.0f};
                    break;
                case 111: // Terran Barracks
                case 113: // Terran Factory
                case 114: // Terran Starport
                case 116: // Terran Science Facility
                    Half = {56.0f, 40.0f};
                    break;
                case 107: // Terran Comsat Station
                case 108: // Terran Nuclear Silo
                case 109: // Terran Supply Depot
                case 115: // Terran Control Tower
                case 120: // Terran Machine Shop
                case 124: // Terran Missile Turret
                case 125: // Terran Bunker
                    Half = {32.0f, 24.0f};
                    break;
                default:
                    Half = Building->IsAddon ? Core::Math::Vec2{32.0f, 24.0f} :
                        Core::Math::Vec2{48.0f, 32.0f};
                    break;
            }
        }

        return {Half.X, Half.Y, Half.X, Half.Y};
    }

    static void ClearMotion(PathfindingComponent& Path)
    {
        Path.Path.clear();
        Path.PathIndex = 0;
        Path.CurrentSpeed = 0.0f;
        Path.TargetSpeed = 0.0f;
        Path.CurrentVelocity = {0.0f, 0.0f};
        Path.StallTimer = 0.0f;
        Path.HasLastWaypointDistance = false;
        Path.BlockedByEntityID = -1;
    }

    static bool IsMovementCommand(CommandType Type)
    {
        return Type == CommandType::Move ||
               Type == CommandType::AttackMove ||
               Type == CommandType::Attack ||
               Type == CommandType::Patrol ||
               Type == CommandType::Harvest ||
               Type == CommandType::Build ||
               Type == CommandType::Follow;
    }

    static bool ShouldHaveMovementSpeed(const TransformComponent* Trans,
                                        const CommandComponent* Cmd,
                                        const PathfindingComponent& Path)
    {
        if (Path.PathIndex < (int)Path.Path.size())
            return true;
        if (Path.CurrentSpeed > 0.01f || Path.CurrentVelocity.LengthSq() > 0.01f)
            return true;
        if (!Trans || !Cmd || !IsMovementCommand(Cmd->CurrentCommand))
            return false;

        const float ArrivalTolerance =
            std::max(Path.NodeTolerance, (float)CollisionRadiusFromBounds(Path));
        if (Trans->Position.DistanceTo(Path.RequestedDestination) <= ArrivalTolerance)
            return false;
        if (Path.HasRecentMoveFailure &&
            Path.RecentFailedMoveTarget.DistanceTo(Path.RequestedDestination) <=
                std::max(16.0f, ArrivalTolerance))
        {
            return false;
        }
        return true;
    }

    static bool RecentlyFailedCurrentRequest(const PathfindingComponent& Path)
    {
        return Path.HasRecentMoveFailure &&
               Path.RecentFailedMoveTarget.DistanceTo(Path.RequestedDestination) <=
                   std::max(16.0f, Path.NodeTolerance);
    }

    static void MarkMoveTargetFailed(PathfindingComponent& Path)
    {
        if (Path.RequestedDestination.LengthSq() > 0.0f)
        {
            Path.HasRecentMoveFailure = true;
            Path.RecentFailedMoveTarget = Path.RequestedDestination;
        }
        else if (!Path.Path.empty())
        {
            Path.HasRecentMoveFailure = true;
            Path.RecentFailedMoveTarget = Path.Path.back();
        }
        Path.RecentMoveFailureCooldown = 1.0f;
        ClearMotion(Path);
    }

    static bool TrackMovementProgress(PathfindingComponent& Path,
                                      float OldDistance,
                                      float NewDistance,
                                      float AppliedDistance,
                                      float DeltaTime)
    {
        const bool MadeProgress = AppliedDistance > 0.05f &&
                                  NewDistance < OldDistance - 0.25f;
        if (MadeProgress)
        {
            Path.StallTimer = 0.0f;
            Path.LastWaypointDistance = NewDistance;
            Path.HasLastWaypointDistance = true;
            return true;
        }

        if (Path.HasLastWaypointDistance &&
            NewDistance < Path.LastWaypointDistance - 0.25f)
        {
            Path.StallTimer = 0.0f;
            Path.LastWaypointDistance = NewDistance;
            return true;
        }

        Path.StallTimer += DeltaTime;
        Path.LastWaypointDistance = NewDistance;
        Path.HasLastWaypointDistance = true;
        return Path.StallTimer < 0.45f;
    }

    static bool TrackDestinationProgress(PathfindingComponent& Path,
                                         const TransformComponent& Trans,
                                         float AppliedDistance,
                                         float DeltaTime)
    {
        if (Path.RequestedDestination.LengthSq() <= 0.0f)
            return true;
        if (AppliedDistance <= 0.05f)
            return true;

        const float Distance = Trans.Position.DistanceTo(Path.RequestedDestination);
        const float ArrivalTolerance =
            std::max(Path.NodeTolerance, (float)CollisionRadiusFromBounds(Path));
        if (Distance <= ArrivalTolerance)
        {
            Path.DestinationStallTimer = 0.0f;
            Path.LastDestinationDistance = Distance;
            Path.HasLastDestinationDistance = true;
            return true;
        }

        if (!Path.HasLastDestinationDistance ||
            Distance < Path.LastDestinationDistance - 1.0f)
        {
            Path.DestinationStallTimer = 0.0f;
            Path.LastDestinationDistance = Distance;
            Path.HasLastDestinationDistance = true;
            return true;
        }

        Path.DestinationStallTimer += DeltaTime;
        Path.HasLastDestinationDistance = true;
        return Path.DestinationStallTimer < 8.0f;
    }

    static float MovementSpeedMultiplier(const StatusEffectComponent* Status)
    {
        if (!Status)
            return 1.0f;

        int Mod = 0;
        if (Status->HasEffect(StatusEffect::Stim))
            ++Mod;
        if (Status->HasEffect(StatusEffect::Ensnare))
            --Mod;
        if (Mod > 0)
            return 1.5f;
        if (Mod < 0)
            return 0.5f;
        return 1.0f;
    }

    static bool IsWalkableWorldPosition(const Game::Map::GameMap& Map,
                                        const Core::Math::Vec2& Position,
                                        float ClearanceRadius)
    {
        int TileX = 0;
        int TileY = 0;
        Map.WorldToTile(Position, TileX, TileY);
        if (!Map.IsInBounds(TileX, TileY) || !Map.IsWalkable(TileX, TileY))
            return false;
        if (ClearanceRadius <= 16.0f)
            return true;

        const float R = ClearanceRadius - 1.0f;
        const Core::Math::Vec2 Samples[] = {
            {Position.X - R, Position.Y},
            {Position.X + R, Position.Y},
            {Position.X, Position.Y - R},
            {Position.X, Position.Y + R},
            {Position.X - R, Position.Y - R},
            {Position.X + R, Position.Y - R},
            {Position.X - R, Position.Y + R},
            {Position.X + R, Position.Y + R}
        };
        for (const auto& Sample : Samples)
        {
            int SX = 0;
            int SY = 0;
            Map.WorldToTile(Sample, SX, SY);
            if (!Map.IsInBounds(SX, SY) || !Map.IsWalkable(SX, SY))
                return false;
        }
        return true;
    }

    static Core::Math::Vec2 ResolveMapWalkability(const Game::Map::GameMap* Map,
                                                  Core::Math::Vec2 Current,
                                                  Core::Math::Vec2 Proposed,
                                                  float MovingRadius,
                                                  bool IsAir)
    {
        if (IsAir || !Map || IsWalkableWorldPosition(*Map, Proposed, MovingRadius))
            return Proposed;

        const Core::Math::Vec2 SlideX{Proposed.X, Current.Y};
        if (IsWalkableWorldPosition(*Map, SlideX, MovingRadius))
            return SlideX;

        const Core::Math::Vec2 SlideY{Current.X, Proposed.Y};
        if (IsWalkableWorldPosition(*Map, SlideY, MovingRadius))
            return SlideY;

        return Current;
    }

    static Core::Math::Vec2 PreventBackwardStep(Core::Math::Vec2 Current,
                                                Core::Math::Vec2 Proposed,
                                                Core::Math::Vec2 MoveDir)
    {
        const Core::Math::Vec2 Applied = Proposed - Current;
        if (Applied.Dot(MoveDir) >= -0.001f)
            return Proposed;

        const Core::Math::Vec2 Lateral = Applied - MoveDir * Applied.Dot(MoveDir);
        if (Lateral.LengthSq() > 0.001f)
            return Current + Lateral;
        return Current;
    }

    static StaticBlockerRect CollisionRectForPath(const PathfindingComponent& Path)
    {
        StaticBlockerRect Rect;
        if (Path.HasCollisionBounds)
        {
            Rect.Left = (float)Path.CollisionLeft;
            Rect.Top = (float)Path.CollisionUp;
            Rect.Right = (float)Path.CollisionRight + 1.0f;
            Rect.Bottom = (float)Path.CollisionDown + 1.0f;
        }
        else
        {
            const float Radius = (float)std::max(4, Path.CollisionRadius);
            Rect.Left = Radius;
            Rect.Top = Radius;
            Rect.Right = Radius;
            Rect.Bottom = Radius;
        }
        return Rect;
    }

    static Core::Math::Vec2 ResolveStaticBlockers(Core::ECS::EntityManager& EM,
                                                  Core::ECS::EntityID MovingID,
                                                  Core::Math::Vec2 Proposed,
                                                  Core::Math::Vec2 MoveDir,
                                                  bool IsAir)
    {
        if (IsAir)
            return Proposed;

        auto Blockers = EM.GetEntitiesWith<TransformComponent, UnitComponent>();
        auto* MovingPath = EM.GetComponent<PathfindingComponent>(MovingID);
        if (!MovingPath)
            return Proposed;
        for (auto BlockerID : Blockers)
        {
            if (BlockerID == MovingID || !IsStaticGroundBlocker(EM, BlockerID))
                continue;
            // CommandSystem has already planned this order with the explicit
            // target excluded (for example, a worker approaching a mineral
            // field). Applying that target again as a hard runtime blocker
            // leaves the unit parked at the footprint edge with a path that
            // can never finish.
            if (MovingPath && BlockerID == MovingPath->RequestedIgnoreEntityID)
                continue;

            auto* BlockerTrans = EM.GetComponent<TransformComponent>(BlockerID);
            auto* BlockerUnit = EM.GetComponent<UnitComponent>(BlockerID);
            auto* BlockerBuilding = EM.GetComponent<BuildingComponent>(BlockerID);
            auto* BlockerResource = EM.GetComponent<ResourceComponent>(BlockerID);
            auto* BlockerPath = EM.GetComponent<PathfindingComponent>(BlockerID);
            if (!BlockerTrans || !BlockerUnit)
                continue;

            const StaticBlockerRect Rect = StaticBlockerRectForEntity(
                BlockerUnit, BlockerBuilding, BlockerResource, BlockerPath);
            const StaticBlockerRect MovingRect = CollisionRectForPath(*MovingPath);
            const float ExpandedLeft = BlockerTrans->Position.X - Rect.Left - MovingRect.Right;
            const float ExpandedRight = BlockerTrans->Position.X + Rect.Right + MovingRect.Left;
            const float ExpandedTop = BlockerTrans->Position.Y - Rect.Top - MovingRect.Bottom;
            const float ExpandedBottom = BlockerTrans->Position.Y + Rect.Bottom + MovingRect.Top;
            if (Proposed.X <= ExpandedLeft || Proposed.X >= ExpandedRight ||
                Proposed.Y <= ExpandedTop || Proposed.Y >= ExpandedBottom)
                continue;

            if (MovingPath)
                MovingPath->BlockedByEntityID = BlockerID;

            const float PushLeft = Proposed.X - ExpandedLeft;
            const float PushRight = ExpandedRight - Proposed.X;
            const float PushTop = Proposed.Y - ExpandedTop;
            const float PushBottom = ExpandedBottom - Proposed.Y;
            const float MinPushX = std::min(PushLeft, PushRight);
            const float MinPushY = std::min(PushTop, PushBottom);
            if (MinPushX < MinPushY)
            {
                if (PushLeft < PushRight || (PushLeft == PushRight && MoveDir.X >= 0.0f))
                    Proposed.X = ExpandedLeft;
                else
                    Proposed.X = ExpandedRight;
            }
            else
            {
                if (PushTop < PushBottom || (PushTop == PushBottom && MoveDir.Y >= 0.0f))
                    Proposed.Y = ExpandedTop;
                else
                    Proposed.Y = ExpandedBottom;
            }
        }
        return Proposed;
    }

    static StaticBlockerRect DynamicCollisionRectForPath(
        const PathfindingComponent& Path)
    {
        return CollisionRectForPath(Path);
    }

    static float DynamicCollisionOverlap(Core::Math::Vec2 PositionA,
                                         const StaticBlockerRect& RectA,
                                         Core::Math::Vec2 PositionB,
                                         const StaticBlockerRect& RectB)
    {
        const float OverlapX = std::min(PositionA.X + RectA.Right,
                                        PositionB.X + RectB.Right) -
                               std::max(PositionA.X - RectA.Left,
                                        PositionB.X - RectB.Left);
        const float OverlapY = std::min(PositionA.Y + RectA.Bottom,
                                        PositionB.Y + RectB.Bottom) -
                               std::max(PositionA.Y - RectA.Top,
                                        PositionB.Y - RectB.Top);
        if (OverlapX <= 0.0f || OverlapY <= 0.0f)
            return 0.0f;
        return OverlapX * OverlapY;
    }

    static Core::Math::Vec2 DynamicCollisionSeparation(
        Core::Math::Vec2 PositionA,
        const StaticBlockerRect& RectA,
        Core::Math::Vec2 PositionB,
        const StaticBlockerRect& RectB,
        bool PreferPositive)
    {
        const float OverlapX = std::min(PositionA.X + RectA.Right,
                                        PositionB.X + RectB.Right) -
                               std::max(PositionA.X - RectA.Left,
                                        PositionB.X - RectB.Left);
        const float OverlapY = std::min(PositionA.Y + RectA.Bottom,
                                        PositionB.Y + RectB.Bottom) -
                               std::max(PositionA.Y - RectA.Top,
                                        PositionB.Y - RectB.Top);
        if (OverlapX <= 0.0f || OverlapY <= 0.0f)
            return {0.0f, 0.0f};

        if (OverlapX < OverlapY)
        {
            const float Direction = PositionA.X < PositionB.X ? -1.0f :
                (PositionA.X > PositionB.X ? 1.0f : (PreferPositive ? 1.0f : -1.0f));
            return {Direction * OverlapX, 0.0f};
        }

        const float Direction = PositionA.Y < PositionB.Y ? -1.0f :
            (PositionA.Y > PositionB.Y ? 1.0f : (PreferPositive ? 1.0f : -1.0f));
        return {0.0f, Direction * OverlapY};
    }

    static Core::ECS::EntityID FindDynamicUnitCollision(
        Core::ECS::EntityManager& EM,
        Core::ECS::EntityID MovingID,
        Core::Math::Vec2 Current,
        Core::Math::Vec2 Proposed)
    {
        auto* MovingPath = EM.GetComponent<PathfindingComponent>(MovingID);
        if (!MovingPath || MovingPath->IsAir)
            return -1;

        const StaticBlockerRect MovingRect =
            DynamicCollisionRectForPath(*MovingPath);
        auto Units = EM.GetEntitiesWith<TransformComponent,
                                        UnitComponent,
                                        PathfindingComponent>();
        for (auto OtherID : Units)
        {
            if (OtherID == MovingID || IsStaticGroundBlocker(EM, OtherID))
                continue;

            auto* OtherTrans = EM.GetComponent<TransformComponent>(OtherID);
            auto* OtherUnit = EM.GetComponent<UnitComponent>(OtherID);
            auto* OtherPath = EM.GetComponent<PathfindingComponent>(OtherID);
            if (!OtherTrans || !OtherUnit || !OtherPath ||
                OtherUnit->HitPoints <= 0 || OtherPath->IsAir)
            {
                continue;
            }

            const StaticBlockerRect OtherRect =
                DynamicCollisionRectForPath(*OtherPath);
            const float CurrentOverlap = DynamicCollisionOverlap(
                Current, MovingRect, OtherTrans->Position, OtherRect);
            const float ProposedOverlap = DynamicCollisionOverlap(
                Proposed, MovingRect, OtherTrans->Position, OtherRect);
            // Existing overlaps can occur during spawn, map load, or dense group
            // movement. Leave those to the soft repulse field so units can
            // separate; the hard boundary prevents a separated pair from
            // entering a new overlap.
            if (CurrentOverlap <= 0.0f && ProposedOverlap > 0.0f)
            {
                return OtherID;
            }
        }
        return -1;
    }

    static Core::Math::Vec2 ResolveDynamicUnitBlockers(
        Core::ECS::EntityManager& EM,
        Core::ECS::EntityID MovingID,
        Core::Math::Vec2 Current,
        Core::Math::Vec2 Proposed,
        Core::Math::Vec2 MoveDir,
        bool IsAir)
    {
        if (IsAir)
            return Proposed;

        const Core::ECS::EntityID BlockerID =
            FindDynamicUnitCollision(EM, MovingID, Current, Proposed);
        if (BlockerID < 0)
            return Proposed;

        if (auto* MovingPath = EM.GetComponent<PathfindingComponent>(MovingID))
            MovingPath->BlockedByEntityID = BlockerID;

        // A friendly mobile unit is a soft reservation. Ask it to yield a small
        // amount instead of forcing the whole selected group to wait forever.
        const auto* MovingPlayer = EM.GetComponent<PlayerComponent>(MovingID);
        const auto* BlockerPlayer = EM.GetComponent<PlayerComponent>(BlockerID);
        auto* BlockerPath = EM.GetComponent<PathfindingComponent>(BlockerID);
        auto* BlockerTrans = EM.GetComponent<TransformComponent>(BlockerID);
        const auto* BlockerCommand = EM.GetComponent<CommandComponent>(BlockerID);
        if (MovingPlayer && BlockerPlayer && BlockerPath && BlockerTrans &&
            MovingPlayer->PlayerID == BlockerPlayer->PlayerID &&
            CanMoveEntity(EM, BlockerID) &&
            (!BlockerCommand || BlockerCommand->CurrentCommand != CommandType::HoldPosition))
        {
            Core::Math::Vec2 Lateral{-MoveDir.Y, MoveDir.X};
            if (MovingID > BlockerID)
                Lateral = Lateral * -1.0f;
            const Core::Math::Vec2 YieldDirection =
                (MoveDir * 0.35f + Lateral).Normalized();
            BlockerPath->FriendlyYieldVector =
                BlockerPath->FriendlyYieldVector + YieldDirection;
        }

        const Core::Math::Vec2 SlideX{Proposed.X, Current.Y};
        if ((SlideX - Current).LengthSq() > 0.001f &&
            FindDynamicUnitCollision(EM, MovingID, Current, SlideX) < 0)
        {
            return SlideX;
        }

        const Core::Math::Vec2 SlideY{Current.X, Proposed.Y};
        if ((SlideY - Current).LengthSq() > 0.001f &&
            FindDynamicUnitCollision(EM, MovingID, Current, SlideY) < 0)
        {
            return SlideY;
        }

        const float Step = (Proposed - Current).Length();
        if (Step > 0.001f)
        {
            Core::Math::Vec2 Lateral{-MoveDir.Y, MoveDir.X};
            if (MovingID > BlockerID)
                Lateral = Lateral * -1.0f;
            Core::Math::Vec2 Diagonal = (MoveDir * 0.35f + Lateral).Normalized();
            Core::Math::Vec2 Slide = Current + Diagonal * Step;
            if (FindDynamicUnitCollision(EM, MovingID, Current, Slide) < 0)
                return Slide;

            Diagonal = (MoveDir * 0.35f - Lateral).Normalized();
            Slide = Current + Diagonal * Step;
            if (FindDynamicUnitCollision(EM, MovingID, Current, Slide) < 0)
                return Slide;

            Slide = Current + Lateral * Step;
            if (FindDynamicUnitCollision(EM, MovingID, Current, Slide) < 0)
                return Slide;

            Slide = Current - Lateral * Step;
            if (FindDynamicUnitCollision(EM, MovingID, Current, Slide) < 0)
                return Slide;
        }

        return Current;
    }

    static int TerrainClearanceForPath(const PathfindingComponent& Path)
    {
        return Path.CollisionRadius > 16 ? Path.CollisionRadius : 0;
    }

    static bool PathHasMeaningfulProgress(const TransformComponent& Trans,
                                          const PathfindingComponent& Path,
                                          const Core::Math::Vec2& Target,
                                          const std::vector<Core::Math::Vec2>& Waypoints)
    {
        if (Waypoints.empty())
            return false;

        const Core::Math::Vec2 Last = Waypoints.back();
        const float ArrivalTolerance =
            std::max(Path.NodeTolerance, (float)CollisionRadiusFromBounds(Path));
        if (Trans.Position.DistanceTo(Target) <= ArrivalTolerance)
            return true;

        const bool LastApproachesTarget =
            Last.DistanceTo(Target) < Trans.Position.DistanceTo(Target) - ArrivalTolerance;
        return LastApproachesTarget;
    }

    static void RefreshPathToTarget(Core::ECS::EntityManager& EM,
                                    Core::ECS::EntityID EntityID,
                                    const Game::Map::GameMap* Map,
                                    const Core::Math::Vec2& Target,
                                    int IgnoreEntityID = -1,
                                    int AvoidDynamicEntityID = -1)
    {
        auto* Trans = EM.GetComponent<TransformComponent>(EntityID);
        auto* Path = EM.GetComponent<PathfindingComponent>(EntityID);
        if (!Trans || !Path)
            return;

        const bool DestinationChanged =
            Path->RequestedDestination.DistanceTo(Target) > 1.0f ||
            Path->RequestedIgnoreEntityID != IgnoreEntityID;
        Path->RequestedDestination = Target;
        Path->RequestedIgnoreEntityID = IgnoreEntityID;
        Path->Path.clear();
        Path->PathIndex = 0;
        Path->BlockedByEntityID = -1;
        if (DestinationChanged)
        {
            Path->DestinationStallTimer = 0.0f;
            Path->HasLastDestinationDistance = false;
        }

        if (Path->IsAir || !Map)
        {
            Path->Path.push_back(Target);
            return;
        }

        int StartTileX = 0;
        int StartTileY = 0;
        Map->WorldToTile(Trans->Position, StartTileX, StartTileY);
        const auto* MovingPlayer = EM.GetComponent<PlayerComponent>(EntityID);
        std::vector<StaticBlockerRect> Blockers;
        for (auto BlockerID : EM.GetEntitiesWith<TransformComponent, UnitComponent>())
        {
            if (BlockerID == IgnoreEntityID)
                continue;
            if (BlockerID == EntityID)
                continue;

            auto* BlockerTrans = EM.GetComponent<TransformComponent>(BlockerID);
            auto* BlockerUnit = EM.GetComponent<UnitComponent>(BlockerID);
            auto* BlockerBuilding = EM.GetComponent<BuildingComponent>(BlockerID);
            auto* BlockerResource = EM.GetComponent<ResourceComponent>(BlockerID);
            auto* BlockerPath = EM.GetComponent<PathfindingComponent>(BlockerID);
            if (!BlockerTrans || !BlockerUnit)
                continue;

            const bool IsStaticBlocker =
                IsStaticGroundBlocker(EM, BlockerID);
            const bool IsRequestedDynamicAvoid =
                BlockerID == AvoidDynamicEntityID;
            const auto* BlockerPlayer =
                EM.GetComponent<PlayerComponent>(BlockerID);
            const bool IsStoppedFriendlyDynamicAvoid =
                AvoidDynamicEntityID >= 0 &&
                !IsStaticBlocker &&
                MovingPlayer &&
                BlockerPlayer &&
                MovingPlayer->PlayerID == BlockerPlayer->PlayerID &&
                BlockerPath &&
                BlockerPath->CurrentVelocity.LengthSq() <= 0.01f;
            if (!IsStaticBlocker &&
                !IsRequestedDynamicAvoid &&
                !IsStoppedFriendlyDynamicAvoid)
            {
                continue;
            }

            auto Rect = StaticBlockerRectForEntity(
                BlockerUnit, BlockerBuilding, BlockerResource, BlockerPath);
            const StaticBlockerRect MovingRect = CollisionRectForPath(*Path);
            Rect.Left += MovingRect.Right;
            Rect.Top += MovingRect.Bottom;
            Rect.Right += MovingRect.Left;
            Rect.Bottom += MovingRect.Top;
            Rect.Position = BlockerTrans->Position;
            Blockers.push_back(Rect);
        }

        const std::function<bool(int, int)> ExtraBlockedTile =
            [Blockers = std::move(Blockers), Map, StartTileX, StartTileY](int TileX, int TileY)
        {
            if (TileX == StartTileX && TileY == StartTileY)
                return false;

            const Core::Math::Vec2 Center = Map->TileToWorld(TileX, TileY);
            for (const auto& Rect : Blockers)
            {
                if (Center.X >= Rect.Position.X - Rect.Left &&
                    Center.X <= Rect.Position.X + Rect.Right &&
                    Center.Y >= Rect.Position.Y - Rect.Top &&
                    Center.Y <= Rect.Position.Y + Rect.Bottom)
                {
                    return true;
                }
            }
            return false;
        };

        auto Result = Game::Pathfinding::AStar::FindPath(
            *Map,
            Trans->Position,
            Target,
            1000,
            TerrainClearanceForPath(*Path),
            ExtraBlockedTile);
        if (Result.Found &&
            PathHasMeaningfulProgress(*Trans, *Path, Target, Result.Waypoints))
        {
            Path->Path = std::move(Result.Waypoints);
        }
    }

    static Core::Math::Vec2 ResolveEdgeApproachTarget(Core::ECS::EntityManager& EM,
                                                      const Core::Math::Vec2& Current,
                                                      int TargetEntityID,
                                                      const Core::Math::Vec2& Fallback)
    {
        auto* TargetTrans = EM.GetComponent<TransformComponent>(TargetEntityID);
        auto* TargetPath = EM.GetComponent<PathfindingComponent>(TargetEntityID);
        if (!TargetTrans || !TargetPath || !TargetPath->HasCollisionBounds)
            return Fallback;

        const Core::Math::Vec2 Delta = Current - TargetTrans->Position;
        const float Left = (float)TargetPath->CollisionLeft;
        const float Right = (float)TargetPath->CollisionRight;
        const float Up = (float)TargetPath->CollisionUp;
        const float Down = (float)TargetPath->CollisionDown;
        const float ClampedX = std::clamp(Delta.X, -Left, Right);
        const float ClampedY = std::clamp(Delta.Y, -Up, Down);
        const bool HorizontalPrimary = std::fabs(Delta.X) >= std::fabs(Delta.Y);
        Core::Math::Vec2 Result = TargetTrans->Position;
        if (HorizontalPrimary)
        {
            Result.X += Delta.X < 0.0f ? -Left - 12.0f : Right + 12.0f;
            Result.Y += ClampedY;
        }
        else
        {
            Result.X += ClampedX;
            Result.Y += Delta.Y < 0.0f ? -Up - 12.0f : Down + 12.0f;
        }
        return Result;
    }

    static bool RefreshPathForCurrentRequest(Core::ECS::EntityManager& EM,
                                             Core::ECS::EntityID EntityID,
                                             const Game::Map::GameMap* Map,
                                             bool UseEdgeTarget,
                                             int AvoidDynamicEntityID = -1)
    {
        auto* Path = EM.GetComponent<PathfindingComponent>(EntityID);
        auto* Trans = EM.GetComponent<TransformComponent>(EntityID);
        if (!Path || !Trans)
            return false;

        Core::Math::Vec2 Target = Path->RequestedDestination;
        const Core::Math::Vec2 FinalDestination = Path->RequestedDestination;
        const int RequestedTargetEntityID = Path->RequestedTargetEntityID;
        const int RequestedIgnoreEntityID = Path->RequestedIgnoreEntityID;
        int TargetEntityID = Path->RequestedTargetEntityID >= 0
            ? Path->RequestedTargetEntityID
            : Path->RequestedIgnoreEntityID;
        if (Path->RequestedTargetEntityID >= 0)
        {
            if (auto* TargetTrans = EM.GetComponent<TransformComponent>(Path->RequestedTargetEntityID))
                Target = TargetTrans->Position;
        }
        if (UseEdgeTarget && TargetEntityID >= 0)
            Target = ResolveEdgeApproachTarget(EM, Trans->Position, TargetEntityID, Target);

        RefreshPathToTarget(EM, EntityID, Map, Target,
                            Path->RequestedIgnoreEntityID,
                            AvoidDynamicEntityID);
        if (auto* RefreshedPath = EM.GetComponent<PathfindingComponent>(EntityID))
        {
            RefreshedPath->RequestedDestination = FinalDestination;
            RefreshedPath->RequestedTargetEntityID = RequestedTargetEntityID;
            RefreshedPath->RequestedIgnoreEntityID = RequestedIgnoreEntityID;
            return !RefreshedPath->Path.empty();
        }
        return false;
    }

    static bool HandleMovementStall(Core::ECS::EntityManager& EM,
                                    Core::ECS::EntityID EntityID,
                                    PathfindingComponent& Path,
                                    const Game::Map::GameMap* Map)
    {
        if (Path.RepathFailCount == 0)
        {
            Path.RepathFailCount = 1;
            Path.LastRepathTime = 0.0f;
            return RefreshPathForCurrentRequest(EM, EntityID, Map, false);
        }
        if (Path.RepathFailCount == 1)
        {
            Path.RepathFailCount = 2;
            Path.LastRepathTime = 0.0f;
            return RefreshPathForCurrentRequest(EM, EntityID, Map, true);
        }

        MarkMoveTargetFailed(Path);
        Path.RepathFailCount = 0;
        return false;
    }

    static bool IsTemporaryUnitBlocker(Core::ECS::EntityManager& EM,
                                       const PathfindingComponent& Path)
    {
        if (Path.BlockedByEntityID < 0 ||
            IsStaticGroundBlocker(EM, Path.BlockedByEntityID))
        {
            return false;
        }

        const auto* Unit = EM.GetComponent<UnitComponent>(Path.BlockedByEntityID);
        const auto* OtherPath = EM.GetComponent<PathfindingComponent>(Path.BlockedByEntityID);
        if (!Unit || Unit->HitPoints <= 0 || !OtherPath || OtherPath->IsAir)
            return false;

        // Dynamic units are transient reservations, not permanent terrain.
        // Re-running A* against the unchanged static map returns the same path
        // and eventually blacklists a perfectly reachable group destination.
        return true;
    }

    static bool IsFriendlyTemporaryUnitBlocker(Core::ECS::EntityManager& EM,
                                               Core::ECS::EntityID MovingID,
                                               const PathfindingComponent& Path)
    {
        if (!IsTemporaryUnitBlocker(EM, Path))
            return false;
        const auto* MovingPlayer = EM.GetComponent<PlayerComponent>(MovingID);
        const auto* BlockingPlayer =
            EM.GetComponent<PlayerComponent>(Path.BlockedByEntityID);
        return MovingPlayer && BlockingPlayer &&
               MovingPlayer->PlayerID == BlockingPlayer->PlayerID;
    }

    static constexpr float kRepulseForce = 80.0f;     // push force scalar
} // anonymous namespace

void MovementSystem::Update(float DeltaTime)
{
    if (DeltaTime <= 0.0f) return;

    auto& EM = mSystemMgr.GetEntityManager();
    auto Entities = EM.GetEntitiesWith<TransformComponent, PathfindingComponent>();

    // Clamp delta to prevent large jumps on frame spikes
    float Dt = std::min(DeltaTime, 0.1f);

    // --- Initialize new movement fields for entities missing them ---
    for (auto ID : Entities)
    {
        auto* PathComp = EM.GetComponent<PathfindingComponent>(ID);
        auto* Unit = EM.GetComponent<UnitComponent>(ID);
        auto* Trans = EM.GetComponent<TransformComponent>(ID);
        auto* Cmd = EM.GetComponent<CommandComponent>(ID);
        if (!PathComp) continue;
        // BulletSystem owns projectile integration. The path component exists
        // only to expose the flight direction to the IScript animation layer.
        if (EM.GetComponent<BulletComponent>(ID))
            continue;

        if (PathComp->HasRecentMoveFailure)
        {
            PathComp->RecentMoveFailureCooldown =
                std::max(0.0f, PathComp->RecentMoveFailureCooldown - Dt);
            if (PathComp->RecentMoveFailureCooldown <= 0.0f)
                PathComp->HasRecentMoveFailure = false;
        }

        if (Unit)
            PathComp->IsAir = ShouldMoveAsAir(EM, ID);

        if (!CanMoveEntity(EM, ID))
        {
            ClearMotion(*PathComp);
            PathComp->RepulseVector = {0.0f, 0.0f};
            continue;
        }

        const bool HasMovementSpeedIntent =
            ShouldHaveMovementSpeed(Trans, Cmd, *PathComp);
        if (!HasMovementSpeedIntent)
        {
            PathComp->TargetSpeed = 0.0f;
            PathComp->CurrentSpeed = 0.0f;
            PathComp->CurrentVelocity = {0.0f, 0.0f};
            PathComp->RepulseVector = {0.0f, 0.0f};
            continue;
        }
        if (!Cmd || Cmd->CurrentCommand != CommandType::Stop)
            PathComp->HasFriendlyYieldAnchor = false;

        // Set defaults from unit data if not yet initialized
        if (PathComp->TargetSpeed <= 0.0f && Unit)
        {
            PathComp->TargetSpeed = Tuning::ToWorldMoveSpeed(Unit->Speed);
            PathComp->Acceleration = 200.0f;
            PathComp->TurnRate = 8.0f;
            if (PathComp->HasCollisionBounds)
                PathComp->CollisionRadius = CollisionRadiusFromBounds(*PathComp);
            else
            {
                PathComp->CollisionRadius = 12;
                if (Unit->SizeType == UnitSize::Large)
                    PathComp->CollisionRadius = 20;
            }
        }
        else if (PathComp->TargetSpeed <= 0.0f)
        {
            PathComp->TargetSpeed = Tuning::ToWorldMoveSpeed(2.0f);
            PathComp->Acceleration = 200.0f;
            PathComp->TurnRate = 6.0f;
            PathComp->CollisionRadius = PathComp->HasCollisionBounds ?
                CollisionRadiusFromBounds(*PathComp) : 16;
        }

        // Per-frame accumulation
        PathComp->RepulseVector = {0.0f, 0.0f};
    }

    // --- Repulse field: collect all unit positions ---
    // For each unit, check nearby units and compute push vectors
    for (auto ID : Entities)
    {
        auto* TransA = EM.GetComponent<TransformComponent>(ID);
        auto* PathA = EM.GetComponent<PathfindingComponent>(ID);
        if (!TransA || !PathA || PathA->IsAir) continue;

        for (auto ID2 : Entities)
        {
            if (ID == ID2) continue;
            auto* TransB = EM.GetComponent<TransformComponent>(ID2);
            auto* PathB = EM.GetComponent<PathfindingComponent>(ID2);
            if (!TransB || !PathB || PathB->IsAir) continue;

            const StaticBlockerRect RectA = CollisionRectForPath(*PathA);
            const StaticBlockerRect RectB = CollisionRectForPath(*PathB);
            const Core::Math::Vec2 Separation = DynamicCollisionSeparation(
                TransA->Position, RectA, TransB->Position, RectB, ID > ID2);
            const float Overlap = Separation.Length();
            if (Overlap > 0.001f)
            {
                const Core::Math::Vec2 Push = Separation.Normalized();
                float Force = Overlap * kRepulseForce * Dt;
                PathA->RepulseVector = PathA->RepulseVector + Push * Force;
            }
        }

        auto Blockers = EM.GetEntitiesWith<TransformComponent, UnitComponent>();
        for (auto BlockerID : Blockers)
        {
            if (BlockerID == ID || !IsStaticGroundBlocker(EM, BlockerID))
                continue;

            auto* BlockerTrans = EM.GetComponent<TransformComponent>(BlockerID);
            auto* BlockerUnit = EM.GetComponent<UnitComponent>(BlockerID);
            auto* BlockerBuilding = EM.GetComponent<BuildingComponent>(BlockerID);
            auto* BlockerResource = EM.GetComponent<ResourceComponent>(BlockerID);
            auto* BlockerPath = EM.GetComponent<PathfindingComponent>(BlockerID);
            if (!BlockerTrans || !BlockerUnit)
                continue;

            const StaticBlockerRect Rect = StaticBlockerRectForEntity(
                BlockerUnit, BlockerBuilding, BlockerResource, BlockerPath);
            const StaticBlockerRect MovingRect = CollisionRectForPath(*PathA);
            const Core::Math::Vec2 Separation = DynamicCollisionSeparation(
                TransA->Position, MovingRect,
                BlockerTrans->Position, Rect, ID > BlockerID);
            const float Overlap = Separation.Length();
            if (Overlap > 0.001f)
            {
                const Core::Math::Vec2 Push = Separation.Normalized();
                float Force = Overlap * kRepulseForce * Dt;
                PathA->RepulseVector = PathA->RepulseVector + Push * Force;
            }
        }
    }

    // --- Movement update ---
    static std::unordered_map<Core::ECS::EntityID, Core::Math::Vec2> sPatrolStart;

    for (auto ID : Entities)
    {
        auto* Trans = EM.GetComponent<TransformComponent>(ID);
        auto* PathComp = EM.GetComponent<PathfindingComponent>(ID);
        auto* Cmd = EM.GetComponent<CommandComponent>(ID);

        if (!Trans || !PathComp) continue;
        const Core::Math::Vec2 FriendlyYield = PathComp->FriendlyYieldVector;
        // A request created after this entity's turn remains queued for the
        // next frame; a request already present is consumed exactly once here.
        PathComp->FriendlyYieldVector = {0.0f, 0.0f};
        auto& Path = PathComp->Path;

        if (!CanMoveEntity(EM, ID))
        {
            ClearMotion(*PathComp);
            continue;
        }

        PathComp->LastRepathTime += Dt;

        // Stasis / lockdown / maelstrom: frozen, skip all movement
        auto* Status = EM.GetComponent<StatusEffectComponent>(ID);
        if (Status && (Status->HasEffect(StatusEffect::Stasis) ||
                       Status->HasEffect(StatusEffect::Lockdown) ||
                       Status->HasEffect(StatusEffect::Maelstrom)))
        {
            PathComp->CurrentSpeed = 0.0f;
            PathComp->CurrentVelocity = {0.0f, 0.0f};
            continue;
        }

        // Stopped friendly units may step aside briefly. The nudge is bounded,
        // terrain/static-blocker checked, and rejected if it would enter a new
        // unit overlap. Hold-position remains a hard reservation.
        if (FriendlyYield.LengthSq() > 0.001f &&
            (!Cmd || Cmd->CurrentCommand != CommandType::HoldPosition))
        {
            constexpr float kFriendlyYieldSpeed = 24.0f;
            constexpr float kFriendlyYieldMaxDistance = 24.0f;
            if (!PathComp->HasFriendlyYieldAnchor)
            {
                PathComp->FriendlyYieldAnchor = Trans->Position;
                PathComp->HasFriendlyYieldAnchor = true;
            }
            const Core::Math::Vec2 ExistingYield =
                Trans->Position - PathComp->FriendlyYieldAnchor;
            const Core::Math::Vec2 YieldDir = ExistingYield.LengthSq() > 1.0f
                ? ExistingYield.Normalized()
                : FriendlyYield.Normalized();
            Core::Math::Vec2 Yielded =
                Trans->Position + YieldDir * (kFriendlyYieldSpeed * Dt);
            const Core::Math::Vec2 FromAnchor =
                Yielded - PathComp->FriendlyYieldAnchor;
            if (FromAnchor.Length() > kFriendlyYieldMaxDistance)
            {
                Yielded = PathComp->FriendlyYieldAnchor +
                    FromAnchor.Normalized() * kFriendlyYieldMaxDistance;
            }
            Yielded = ResolveStaticBlockers(EM, ID, Yielded, YieldDir, PathComp->IsAir);
            Yielded = ResolveMapWalkability(
                mMap, Trans->Position, Yielded,
                (float)TerrainClearanceForPath(*PathComp), PathComp->IsAir);
            if (FindDynamicUnitCollision(EM, ID, Trans->Position, Yielded) < 0)
                Trans->Position = Yielded;
        }

        // Stop/HoldPosition: clear path, don't move.
        if (Cmd && (Cmd->CurrentCommand == CommandType::Stop ||
                    Cmd->CurrentCommand == CommandType::HoldPosition))
        {
            ClearMotion(*PathComp);
            continue;
        }

        // Follow: re-issue move toward target if path is empty
        if (Cmd && Cmd->CurrentCommand == CommandType::Follow && Path.empty())
        {
            if (Cmd->TargetEntityID >= 0)
            {
                auto* TTrans = EM.GetComponent<TransformComponent>(Cmd->TargetEntityID);
                if (TTrans)
                {
                    float Dist = Trans->Position.DistanceTo(TTrans->Position);
                    if (Dist > 32.0f)
                    {
                        Cmd->TargetPosition = TTrans->Position;
                        RefreshPathToTarget(EM, ID, mMap, TTrans->Position);
                    }
                }
            }
            continue;
        }

        if (Path.empty())
        {
            if (Cmd &&
                (Cmd->CurrentCommand == CommandType::Move ||
                 Cmd->CurrentCommand == CommandType::AttackMove ||
                 Cmd->CurrentCommand == CommandType::Follow ||
                 Cmd->CurrentCommand == CommandType::Build ||
                 Cmd->CurrentCommand == CommandType::Harvest) &&
                !RecentlyFailedCurrentRequest(*PathComp) &&
                Trans->Position.DistanceTo(PathComp->RequestedDestination) >
                    std::max(PathComp->NodeTolerance, (float)CollisionRadiusFromBounds(*PathComp)))
            {
                if (RefreshPathForCurrentRequest(EM, ID, mMap, false))
                    continue;
                MarkMoveTargetFailed(*PathComp);
                if (Cmd && Cmd->HasPendingOrders())
                    CommandSystem::AdvanceQueue(EM, ID);
                continue;
            }

            // Patrol: reverse direction when reaching waypoint
            if (Cmd && Cmd->CurrentCommand == CommandType::Patrol)
            {
                auto It = sPatrolStart.find(ID);
                Core::Math::Vec2 Origin = (It != sPatrolStart.end())
                    ? It->second : Trans->Position;
                if (Origin.DistanceTo(Cmd->TargetPosition) < 8.0f)
                    CommandSystem::IssueMove(EM, {ID}, Origin);
                else
                    CommandSystem::IssueMove(EM, {ID}, Cmd->TargetPosition);
            }
            else
            {
                // Reached destination: advance order queue
                if (Cmd && Cmd->HasPendingOrders())
                    CommandSystem::AdvanceQueue(EM, ID);

                PathComp->TargetSpeed = 0.0f;
                PathComp->CurrentSpeed = 0.0f;
                PathComp->CurrentVelocity = {0.0f, 0.0f};
            }
            continue;
        }

        // Record patrol start
        if (Cmd && Cmd->CurrentCommand == CommandType::Patrol &&
            sPatrolStart.find(ID) == sPatrolStart.end())
        {
            sPatrolStart[ID] = Trans->Position;
        }

        Core::Math::Vec2 MoveDir;

        // Follow next waypoint
        while (PathComp->PathIndex < (int)Path.size())
        {
            Core::Math::Vec2& Waypoint = Path[PathComp->PathIndex];
            Core::Math::Vec2 ToTarget = {Waypoint.X - Trans->Position.X,
                                         Waypoint.Y - Trans->Position.Y};
            float Dist = ToTarget.Length();
            const float MovingRadius = (float)CollisionRadiusFromBounds(*PathComp);
            const bool IsFinalWaypoint = PathComp->PathIndex >= (int)Path.size() - 1;
            const float ArrivalTolerance = IsFinalWaypoint
                ? std::max(PathComp->NodeTolerance, MovingRadius)
                : PathComp->NodeTolerance;

            if (Dist < ArrivalTolerance)
            {
                PathComp->PathIndex++;
                PathComp->StallTimer = 0.0f;
                PathComp->HasLastWaypointDistance = false;
                continue;
            }

            // Speed management with acceleration
            const float TargetSpd =
                PathComp->TargetSpeed *
                MovementSpeedMultiplier(Status) *
                UpgradeSystem::GetMovementSpeedMultiplier(EM, ID);
            float CurSpd = PathComp->CurrentSpeed;
            if (CurSpd < TargetSpd)
                CurSpd = std::min(TargetSpd, CurSpd + PathComp->Acceleration * Dt);
            else if (CurSpd > TargetSpd)
                CurSpd = std::max(TargetSpd, CurSpd - PathComp->Acceleration * Dt * 2.0f);

            PathComp->CurrentSpeed = CurSpd;

            // Desired move direction (toward waypoint)
            MoveDir = ToTarget.Normalized();

            // Apply repulse vector (push away from nearby units)
            Core::Math::Vec2 ActualMove = MoveDir;
            if (PathComp->RepulseVector.Length() > 0.0f)
            {
                Core::Math::Vec2 Avoid = PathComp->RepulseVector.Normalized();
                Core::Math::Vec2 LateralAvoid = Avoid - MoveDir * Avoid.Dot(MoveDir);
                if (LateralAvoid.Length() > 0.001f)
                {
                    ActualMove = (MoveDir + LateralAvoid.Normalized() * 0.45f).Normalized();
                }
            }

            // Turn-rate smoothing
            float DesiredAngle = atan2f(ActualMove.Y, ActualMove.X);
            float Current = PathComp->FacingAngle;
            float Diff = DesiredAngle - Current;
            // Normalize to [-PI, PI]
            while (Diff > 3.14159265359f) Diff -= 6.28318530718f;
            while (Diff < -3.14159265359f) Diff += 6.28318530718f;
            float MaxTurn = PathComp->TurnRate * Dt;
            if (std::fabs(Diff) <= MaxTurn)
                PathComp->FacingAngle = DesiredAngle;
            else
                PathComp->FacingAngle += (Diff > 0.0f ? MaxTurn : -MaxTurn);

            // Move along the actual direction (not facing)
            float MoveAmount = CurSpd * Dt;
            PathComp->BlockedByEntityID = -1;
            if (MoveAmount > Dist)
            {
                const float OldDistance = Dist;
                Core::Math::Vec2 Proposed = ResolveStaticBlockers(
                    EM, ID, Waypoint, MoveDir, PathComp->IsAir);
                Proposed = ResolveMapWalkability(mMap, Trans->Position, Proposed,
                    MovingRadius, PathComp->IsAir);
                Proposed = ResolveDynamicUnitBlockers(
                    EM, ID, Trans->Position, Proposed, MoveDir, PathComp->IsAir);
                Proposed = ResolveStaticBlockers(
                    EM, ID, Proposed, MoveDir, PathComp->IsAir);
                Proposed = ResolveMapWalkability(mMap, Trans->Position, Proposed,
                    MovingRadius, PathComp->IsAir);
                Proposed = PreventBackwardStep(Trans->Position, Proposed, MoveDir);
                Proposed = ResolveStaticBlockers(
                    EM, ID, Proposed, MoveDir, PathComp->IsAir);
                Proposed = ResolveMapWalkability(mMap, Trans->Position, Proposed,
                    MovingRadius, PathComp->IsAir);
                if (const auto BlockerID = FindDynamicUnitCollision(
                        EM, ID, Trans->Position, Proposed);
                    BlockerID >= 0)
                {
                    PathComp->BlockedByEntityID = BlockerID;
                    Proposed = Trans->Position;
                }
                const Core::Math::Vec2 AppliedMove = Proposed - Trans->Position;
                Trans->Position = Proposed;
                const float AppliedDistance = AppliedMove.Length();
                const float NewDistance = (Waypoint - Trans->Position).Length();
                if (!PathComp->IsAir &&
                    (!TrackMovementProgress(*PathComp, OldDistance, NewDistance,
                                            AppliedDistance, Dt) ||
                     !TrackDestinationProgress(*PathComp, *Trans,
                                               AppliedDistance, Dt)))
                {
                    if (IsFriendlyTemporaryUnitBlocker(EM, ID, *PathComp))
                    {
                        const int AvoidEntityID = PathComp->BlockedByEntityID;
                        if (mMap && RefreshPathForCurrentRequest(
                                EM, ID, mMap, false, AvoidEntityID))
                        {
                            PathComp->StallTimer = 0.0f;
                            PathComp->HasLastWaypointDistance = false;
                            break;
                        }
                        PathComp->StallTimer = 0.0f;
                        PathComp->HasLastWaypointDistance = false;
                        PathComp->CurrentSpeed = 0.0f;
                        PathComp->CurrentVelocity = {0.0f, 0.0f};
                        break;
                    }
                    if (!HandleMovementStall(EM, ID, *PathComp, mMap))
                        break;
                    break;
                }
                if (NewDistance <= ArrivalTolerance)
                {
                    PathComp->PathIndex++;
                    PathComp->StallTimer = 0.0f;
                    PathComp->HasLastWaypointDistance = false;
                }
                PathComp->CurrentVelocity = AppliedMove.Length() > 0.001f ?
                    AppliedMove.Normalized() * TargetSpd :
                    Core::Math::Vec2{};
            }
            else
            {
                const float OldDistance = Dist;
                Core::Math::Vec2 Proposed = Trans->Position + ActualMove * MoveAmount;
                Proposed = ResolveStaticBlockers(
                    EM, ID, Proposed, ActualMove, PathComp->IsAir);
                Proposed = ResolveMapWalkability(mMap, Trans->Position, Proposed,
                    MovingRadius, PathComp->IsAir);
                Proposed = ResolveDynamicUnitBlockers(
                    EM, ID, Trans->Position, Proposed, ActualMove, PathComp->IsAir);
                Proposed = ResolveStaticBlockers(
                    EM, ID, Proposed, ActualMove, PathComp->IsAir);
                Proposed = ResolveMapWalkability(mMap, Trans->Position, Proposed,
                    MovingRadius, PathComp->IsAir);
                Proposed = PreventBackwardStep(Trans->Position, Proposed, MoveDir);
                Proposed = ResolveStaticBlockers(
                    EM, ID, Proposed, MoveDir, PathComp->IsAir);
                Proposed = ResolveMapWalkability(mMap, Trans->Position, Proposed,
                    MovingRadius, PathComp->IsAir);
                if (const auto BlockerID = FindDynamicUnitCollision(
                        EM, ID, Trans->Position, Proposed);
                    BlockerID >= 0)
                {
                    PathComp->BlockedByEntityID = BlockerID;
                    Proposed = Trans->Position;
                }
                const Core::Math::Vec2 AppliedMove = Proposed - Trans->Position;
                Trans->Position = Proposed;
                const float AppliedDistance = AppliedMove.Length();
                const float NewDistance = (Waypoint - Trans->Position).Length();
                if (!PathComp->IsAir &&
                    (!TrackMovementProgress(*PathComp, OldDistance, NewDistance,
                                            AppliedDistance, Dt) ||
                     !TrackDestinationProgress(*PathComp, *Trans,
                                               AppliedDistance, Dt)))
                {
                    if (IsFriendlyTemporaryUnitBlocker(EM, ID, *PathComp))
                    {
                        const int AvoidEntityID = PathComp->BlockedByEntityID;
                        if (mMap && RefreshPathForCurrentRequest(
                                EM, ID, mMap, false, AvoidEntityID))
                        {
                            PathComp->StallTimer = 0.0f;
                            PathComp->HasLastWaypointDistance = false;
                            break;
                        }
                        PathComp->StallTimer = 0.0f;
                        PathComp->HasLastWaypointDistance = false;
                        PathComp->CurrentSpeed = 0.0f;
                        PathComp->CurrentVelocity = {0.0f, 0.0f};
                        break;
                    }
                    if (!HandleMovementStall(EM, ID, *PathComp, mMap))
                        break;
                    break;
                }
                PathComp->CurrentVelocity = AppliedMove.Length() > 0.001f ?
                    AppliedMove.Normalized() * CurSpd :
                    Core::Math::Vec2{};
            }
            break;
        }

        if (PathComp->PathIndex >= (int)Path.size())
        {
            const bool NeedsContinuation =
                Trans->Position.DistanceTo(PathComp->RequestedDestination) >
                std::max(PathComp->NodeTolerance, (float)CollisionRadiusFromBounds(*PathComp));
            ClearMotion(*PathComp);
            if (NeedsContinuation &&
                Cmd &&
                (Cmd->CurrentCommand == CommandType::Move ||
                 Cmd->CurrentCommand == CommandType::AttackMove ||
                 Cmd->CurrentCommand == CommandType::Follow ||
                 Cmd->CurrentCommand == CommandType::Build ||
                 Cmd->CurrentCommand == CommandType::Harvest) &&
                !RecentlyFailedCurrentRequest(*PathComp))
            {
                if (!RefreshPathForCurrentRequest(EM, ID, mMap, false))
                {
                    MarkMoveTargetFailed(*PathComp);
                    if (Cmd->HasPendingOrders())
                        CommandSystem::AdvanceQueue(EM, ID);
                }
            }
            else if (Cmd && Cmd->HasPendingOrders())
                CommandSystem::AdvanceQueue(EM, ID);
        }
    }
}

} // namespace Game::Systems
