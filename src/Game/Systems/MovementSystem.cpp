#include "Game/Systems/MovementSystem.h"
#include "Game/Systems/CommandSystem.h"
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
            return Building->IsLifted;
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
            return !Building->IsLifted;
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
        Path.CurrentVelocity = {0.0f, 0.0f};
        Path.StallTimer = 0.0f;
        Path.HasLastWaypointDistance = false;
    }

    static void MarkMoveTargetFailed(PathfindingComponent& Path)
    {
        if (!Path.Path.empty())
        {
            Path.HasRecentMoveFailure = true;
            Path.RecentFailedMoveTarget = Path.Path.back();
        }
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

    static Core::Math::Vec2 ResolveStaticBlockers(Core::ECS::EntityManager& EM,
                                                  Core::ECS::EntityID MovingID,
                                                  Core::Math::Vec2 Proposed,
                                                  Core::Math::Vec2 MoveDir,
                                                  float MovingRadius,
                                                  bool IsAir)
    {
        if (IsAir)
            return Proposed;

        auto Blockers = EM.GetEntitiesWith<TransformComponent, UnitComponent>();
        for (auto BlockerID : Blockers)
        {
            if (BlockerID == MovingID || !IsStaticGroundBlocker(EM, BlockerID))
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
            const float ExpandedLeft = BlockerTrans->Position.X - Rect.Left - MovingRadius;
            const float ExpandedRight = BlockerTrans->Position.X + Rect.Right + MovingRadius;
            const float ExpandedTop = BlockerTrans->Position.Y - Rect.Top - MovingRadius;
            const float ExpandedBottom = BlockerTrans->Position.Y + Rect.Bottom + MovingRadius;
            if (Proposed.X <= ExpandedLeft || Proposed.X >= ExpandedRight ||
                Proposed.Y <= ExpandedTop || Proposed.Y >= ExpandedBottom)
                continue;

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

    static int TerrainClearanceForPath(const PathfindingComponent& Path)
    {
        return Path.CollisionRadius > 16 ? Path.CollisionRadius : 0;
    }

    static void RefreshPathToTarget(Core::ECS::EntityManager& EM,
                                    Core::ECS::EntityID EntityID,
                                    const Game::Map::GameMap* Map,
                                    const Core::Math::Vec2& Target)
    {
        auto* Trans = EM.GetComponent<TransformComponent>(EntityID);
        auto* Path = EM.GetComponent<PathfindingComponent>(EntityID);
        if (!Trans || !Path)
            return;

        Path->Path.clear();
        Path->PathIndex = 0;

        if (Path->IsAir || !Map)
        {
            Path->Path.push_back(Target);
            return;
        }

        int StartTileX = 0;
        int StartTileY = 0;
        Map->WorldToTile(Trans->Position, StartTileX, StartTileY);
        std::vector<StaticBlockerRect> Blockers;
        for (auto BlockerID : EM.GetEntitiesWith<TransformComponent, UnitComponent>())
        {
            if (BlockerID == EntityID || !IsStaticGroundBlocker(EM, BlockerID))
                continue;

            auto* BlockerTrans = EM.GetComponent<TransformComponent>(BlockerID);
            auto* BlockerUnit = EM.GetComponent<UnitComponent>(BlockerID);
            auto* BlockerBuilding = EM.GetComponent<BuildingComponent>(BlockerID);
            auto* BlockerResource = EM.GetComponent<ResourceComponent>(BlockerID);
            auto* BlockerPath = EM.GetComponent<PathfindingComponent>(BlockerID);
            if (!BlockerTrans || !BlockerUnit)
                continue;

            auto Rect = StaticBlockerRectForEntity(
                BlockerUnit, BlockerBuilding, BlockerResource, BlockerPath);
            const float MovingRadius = (float)std::max(4, Path->CollisionRadius);
            Rect.Left += MovingRadius;
            Rect.Top += MovingRadius;
            Rect.Right += MovingRadius;
            Rect.Bottom += MovingRadius;
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
        if (Result.Found && !Result.Waypoints.empty())
            Path->Path = std::move(Result.Waypoints);
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
        if (!PathComp) continue;

        if (Unit)
            PathComp->IsAir = ShouldMoveAsAir(EM, ID);

        if (!CanMoveEntity(EM, ID))
        {
            ClearMotion(*PathComp);
            PathComp->RepulseVector = {0.0f, 0.0f};
            continue;
        }

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

            float Dist = TransA->Position.DistanceTo(TransB->Position);
            float MinDist = (float)(CollisionRadiusFromBounds(*PathA) +
                CollisionRadiusFromBounds(*PathB));

            if (Dist < MinDist && Dist > 0.1f)
            {
                Core::Math::Vec2 Push = (TransA->Position - TransB->Position).Normalized();
                float Overlap = MinDist - Dist;
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
            const Core::Math::Vec2 Delta = TransA->Position - BlockerTrans->Position;
            const float NearestX = std::clamp(Delta.X, -Rect.Left, Rect.Right);
            const float NearestY = std::clamp(Delta.Y, -Rect.Top, Rect.Bottom);
            const Core::Math::Vec2 Nearest{
                BlockerTrans->Position.X + NearestX,
                BlockerTrans->Position.Y + NearestY
            };
            const float MinDist = (float)CollisionRadiusFromBounds(*PathA);
            float Dist = TransA->Position.DistanceTo(Nearest);
            if (Dist < MinDist && Dist > 0.1f)
            {
                Core::Math::Vec2 Push = (TransA->Position - Nearest).Normalized();
                float Overlap = MinDist - Dist;
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
        auto& Path = PathComp->Path;

        if (!CanMoveEntity(EM, ID))
        {
            ClearMotion(*PathComp);
            continue;
        }

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
                PathComp->TargetSpeed * MovementSpeedMultiplier(Status);
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
            if (MoveAmount > Dist)
            {
                const float OldDistance = Dist;
                Core::Math::Vec2 Proposed = ResolveStaticBlockers(
                    EM, ID, Waypoint, MoveDir,
                    MovingRadius, PathComp->IsAir);
                Proposed = ResolveMapWalkability(mMap, Trans->Position, Proposed,
                    MovingRadius, PathComp->IsAir);
                Proposed = PreventBackwardStep(Trans->Position, Proposed, MoveDir);
                const Core::Math::Vec2 AppliedMove = Proposed - Trans->Position;
                Trans->Position = Proposed;
                const float AppliedDistance = AppliedMove.Length();
                const float NewDistance = (Waypoint - Trans->Position).Length();
                if (!PathComp->IsAir &&
                    !TrackMovementProgress(*PathComp, OldDistance, NewDistance,
                                           AppliedDistance, Dt))
                {
                    MarkMoveTargetFailed(*PathComp);
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
                    MoveDir * TargetSpd;
            }
            else
            {
                const float OldDistance = Dist;
                Core::Math::Vec2 Proposed = Trans->Position + ActualMove * MoveAmount;
                Proposed = ResolveStaticBlockers(
                    EM, ID, Proposed, ActualMove,
                    MovingRadius, PathComp->IsAir);
                Proposed = ResolveMapWalkability(mMap, Trans->Position, Proposed,
                    MovingRadius, PathComp->IsAir);
                Proposed = PreventBackwardStep(Trans->Position, Proposed, MoveDir);
                const Core::Math::Vec2 AppliedMove = Proposed - Trans->Position;
                Trans->Position = Proposed;
                const float AppliedDistance = AppliedMove.Length();
                const float NewDistance = (Waypoint - Trans->Position).Length();
                if (!PathComp->IsAir &&
                    !TrackMovementProgress(*PathComp, OldDistance, NewDistance,
                                           AppliedDistance, Dt))
                {
                    MarkMoveTargetFailed(*PathComp);
                    break;
                }
                PathComp->CurrentVelocity = AppliedMove.Length() > 0.001f ?
                    AppliedMove.Normalized() * CurSpd :
                    ActualMove * CurSpd;
            }
            break;
        }

        if (PathComp->PathIndex >= (int)Path.size())
        {
            ClearMotion(*PathComp);
            if (Cmd && Cmd->HasPendingOrders())
                CommandSystem::AdvanceQueue(EM, ID);
        }
    }
}

} // namespace Game::Systems
