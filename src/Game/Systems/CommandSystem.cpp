#include "Game/Systems/CommandSystem.h"
#include "Game/Components.h"
#include "Game/Pathfinding/AStar.h"
#include "Game/Pathfinding/RegionPathfinder.h"
#include "Game/Map/Map.h"
#include "Game/Systems/HarvestSystem.h"
#include "Game/Systems/CombatSystem.h"
#include "Game/Systems/BuildingSystem.h"
#include "Game/Systems/MovementSystem.h"
#include "Game/Systems/BurrowState.h"
#include "Game/Systems/UpgradeSystem.h"
#include "Game/DataEnums.h"
#include "Game/Races/RaceDef.h"
#include "Core/Utils/Logger.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <limits>
#include <vector>

namespace Game::Systems
{

using namespace Game::Components;

const Game::Map::GameMap* gCmdMap = nullptr;
static Game::Pathfinding::RegionPathfinder sRegionPathfinder;
static const Game::Map::GameMap* sRegionPathfinderMap = nullptr;
static CommandSystem::ImageIDResolver sCommandImageResolver;
static CommandSystem::ImageIDResolver sCommandSubunitImageResolver;

static bool PathingDebugEnabled()
{
    const char* Value = std::getenv("STARCRAFT_DEBUG_PATHING");
    return Value && Value[0] != '\0' && Value[0] != '0';
}

static void RebuildRegionPathfinder(const Game::Map::GameMap* Map)
{
    sRegionPathfinderMap = Map;
    if (!Map)
        return;

    Game::Pathfinding::RegionPathfinder::MapContext Ctx;
    Ctx.MapWidth = std::min(
        Map->GetWidth(), Game::Pathfinding::RegionPathfinder::kMaxMapWidth);
    Ctx.MapHeight = std::min(
        Map->GetHeight(), Game::Pathfinding::RegionPathfinder::kMaxMapHeight);

    for (int y = 0; y < Ctx.MapHeight; ++y)
        for (int x = 0; x < Ctx.MapWidth; ++x)
        {
            Ctx.Walkable[x][y] = Map->IsWalkable(x, y);
            Ctx.TileRegion[x][y] = -1;
        }

    sRegionPathfinder.BuildRegions(Ctx);
}

void CommandSystem::SetMap(const Game::Map::GameMap* Map)
{
    gCmdMap = Map;
    RebuildRegionPathfinder(Map);
    MovementSystem::SetPathRequestCallback(&CommandSystem::RequestMovementPath);
}

void CommandSystem::SetImageResolver(ImageIDResolver Resolver)
{
    sCommandImageResolver = std::move(Resolver);
}

void CommandSystem::SetSubunitImageResolver(ImageIDResolver Resolver)
{
    sCommandSubunitImageResolver = std::move(Resolver);
}

static bool ShouldAutoAttackOnGuard(const Components::CombatComponent& Combat)
{
    return Combat.GroundDamage > 0 || Combat.AirDamage > 0;
}

static bool IsMovementOrder(OrderType Type)
{
    return Type == OrderType::Move ||
           Type == OrderType::AttackMove ||
           Type == OrderType::Patrol ||
           Type == OrderType::Follow ||
           Type == OrderType::Guard ||
           Type == OrderType::Repair;
}

static bool IsGroupPositionOrder(OrderType Type)
{
    return Type == OrderType::Move ||
           Type == OrderType::AttackMove ||
           Type == OrderType::Patrol;
}

static bool IsSiegedTankUnit(int ChkUnitID)
{
    return ChkUnitID == 30 ||  // Terran Siege Tank Siege Mode
           ChkUnitID == 25;    // Hero Edmund Duke Siege Mode
}

static bool IsFrozenByStatus(Core::ECS::EntityManager& EM, Core::ECS::EntityID EntityID)
{
    auto* Status = EM.GetComponent<StatusEffectComponent>(EntityID);
    return Status && (Status->HasEffect(StatusEffect::Stasis) ||
                      Status->HasEffect(StatusEffect::Lockdown) ||
                      Status->HasEffect(StatusEffect::Maelstrom));
}

static bool CanReceiveMovementOrder(Core::ECS::EntityManager& EM, Core::ECS::EntityID EntityID)
{
    auto* Trans = EM.GetComponent<TransformComponent>(EntityID);
    auto* Unit = EM.GetComponent<UnitComponent>(EntityID);
    if (!Trans || !Unit)
        return false;
    if (Unit->HitPoints <= 0)
        return false;
    if (Unit->ChkUnitID == 13 || EM.GetComponent<SpiderMineComponent>(EntityID))
        return false;

    if (IsFrozenByStatus(EM, EntityID))
        return false;

    if (EM.GetComponent<ResourceComponent>(EntityID))
        return false;

    if (auto* Burrow = EM.GetComponent<BurrowComponent>(EntityID))
        if (Burrow->IsBurrowed || Burrow->Phase != BurrowPhase::Surface)
            return false;

    if (auto* Building = EM.GetComponent<BuildingComponent>(EntityID))
        return Building->IsLifted &&
               Building->FlightPhase == BuildingFlightPhase::Airborne;

    if (IsSiegedTankUnit(Unit->ChkUnitID))
        return false;

    return Unit->Speed > 0.0f;
}

static bool CanQueueMovementAfterUnburrow(Core::ECS::EntityManager& EM,
                                          Core::ECS::EntityID EntityID)
{
    auto* Burrow = EM.GetComponent<BurrowComponent>(EntityID);
    auto* Command = EM.GetComponent<CommandComponent>(EntityID);
    const Order* CurrentOrder = Command ? Command->GetCurrentOrder() : nullptr;
    return Burrow && Burrow->CanBurrow &&
           (Burrow->Phase == BurrowPhase::Unburrowing ||
            (CurrentOrder && CurrentOrder->Type == OrderType::Unburrow));
}

static bool IsStimTarget(Core::ECS::EntityManager& EM, Core::ECS::EntityID EntityID)
{
    auto* Unit = EM.GetComponent<UnitComponent>(EntityID);
    if (!Unit || Unit->HitPoints <= 10)
        return false;
    const int DefID = Game::Races::GetUnitDefIDByChkUnitID(Unit->ChkUnitID);
    if (DefID < 0)
        return false;
    const auto Def = Game::Races::GetUnitDefByID(DefID);
    return Def.Name == "Marine" || Def.Name == "Firebat";
}

static bool TryApplyUseTechOrder(Core::ECS::EntityManager& EM,
                                 Core::ECS::EntityID EntityID,
                                 const Order& InOrder)
{
    const auto ResearchID = static_cast<ResearchId>(InOrder.AbilityID);
    if (ResearchID != ResearchId::Terran_StimPacks)
        return false;

    auto* Player = EM.GetComponent<PlayerComponent>(EntityID);
    const int PlayerID = Player ? Player->PlayerID : 0;
    if (!UpgradeSystem::HasResearch(PlayerID, ResearchID) ||
        !IsStimTarget(EM, EntityID))
    {
        return false;
    }

    auto* Unit = EM.GetComponent<UnitComponent>(EntityID);
    if (!Unit)
        return false;

    Unit->HitPoints = std::max(1, Unit->HitPoints - 10);
    auto* Status = EM.GetComponent<StatusEffectComponent>(EntityID);
    if (!Status)
        Status = &EM.AddComponent<StatusEffectComponent>(EntityID);
    Status->AddEffect(StatusEffect::Stim, 15.0f, EntityID);
    return true;
}

static void ClearMovementPath(Core::ECS::EntityManager& EM, Core::ECS::EntityID EntityID)
{
    if (auto* Path = EM.GetComponent<PathfindingComponent>(EntityID))
    {
        Path->Path.clear();
        Path->PathIndex = 0;
        Path->CurrentSpeed = 0.0f;
        Path->CurrentVelocity = {0.0f, 0.0f};
        Path->StallTimer = 0.0f;
        Path->HasLastWaypointDistance = false;
        Path->BlockedByEntityID = -1;
    }
}

static int CollisionRadiusForUnit(const UnitComponent* Unit)
{
    if (!Unit)
        return 16;
    if (Unit->SizeType == UnitSize::Large)
        return 20;
    if (Unit->SizeType == UnitSize::Small)
        return 12;
    return 16;
}

static int TerrainClearanceForPath(const PathfindingComponent& Path)
{
    return Path.CollisionRadius > 16 ? Path.CollisionRadius : 0;
}

static int CollisionRadiusFromBounds(const PathfindingComponent& Path)
{
    if (!Path.HasCollisionBounds)
        return Path.CollisionRadius;

    const int Width = Path.CollisionLeft + Path.CollisionRight + 1;
    const int Height = Path.CollisionUp + Path.CollisionDown + 1;
    return std::max(4, std::max(Width, Height) / 2);
}

static void RefreshCollisionRadius(Core::ECS::EntityManager& EM,
                                   Core::ECS::EntityID EntityID,
                                   PathfindingComponent& Path)
{
    if (Path.HasCollisionBounds)
        Path.CollisionRadius = CollisionRadiusFromBounds(Path);
    else
        Path.CollisionRadius = CollisionRadiusForUnit(EM.GetComponent<UnitComponent>(EntityID));
}

static bool ShouldMoveAsAir(Core::ECS::EntityManager& EM, Core::ECS::EntityID EntityID)
{
    if (auto* Building = EM.GetComponent<BuildingComponent>(EntityID))
        if (Building->IsLifted)
            return true;

    auto* Unit = EM.GetComponent<UnitComponent>(EntityID);
    return Unit && Game::Data::IsAirUnitType(Unit->ChkUnitID);
}

struct StaticBlockerRect
{
    float Left = 16.0f;
    float Top = 16.0f;
    float Right = 16.0f;
    float Bottom = 16.0f;
    Core::Math::Vec2 Position;
};

static bool IsStaticGroundBlocker(Core::ECS::EntityManager& EM, Core::ECS::EntityID EntityID)
{
    if (auto* Building = EM.GetComponent<BuildingComponent>(EntityID))
        return !Building->IsLifted ||
               Building->FlightPhase == BuildingFlightPhase::Landing;
    return EM.GetComponent<ResourceComponent>(EntityID) != nullptr;
}

static StaticBlockerRect StaticBlockerRectForEntity(Core::ECS::EntityManager& EM,
                                                    Core::ECS::EntityID EntityID)
{
    StaticBlockerRect Rect;
    if (auto* Trans = EM.GetComponent<TransformComponent>(EntityID))
        Rect.Position = Trans->Position;

    if (auto* Path = EM.GetComponent<PathfindingComponent>(EntityID);
        Path && Path->HasCollisionBounds)
    {
        Rect.Left = (float)Path->CollisionLeft;
        Rect.Top = (float)Path->CollisionUp;
        Rect.Right = (float)Path->CollisionRight + 1.0f;
        Rect.Bottom = (float)Path->CollisionDown + 1.0f;
        return Rect;
    }

    auto* Unit = EM.GetComponent<UnitComponent>(EntityID);
    auto* Building = EM.GetComponent<BuildingComponent>(EntityID);
    auto* Resource = EM.GetComponent<ResourceComponent>(EntityID);
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
                Half = Building->IsAddon ? Core::Math::Vec2{32.0f, 24.0f}
                                         : Core::Math::Vec2{48.0f, 32.0f};
                break;
        }
    }

    Rect.Left = Half.X;
    Rect.Top = Half.Y;
    Rect.Right = Half.X;
    Rect.Bottom = Half.Y;
    return Rect;
}

static StaticBlockerRect ExpandedStaticBlockerRectForMover(
    Core::ECS::EntityManager& EM,
    Core::ECS::EntityID BlockerID,
    const PathfindingComponent& MovingPath)
{
    auto Rect = StaticBlockerRectForEntity(EM, BlockerID);
    // Directional Minkowski expansion. A small asymmetric unit only needs its
    // actual left/right/up/down clearance, not a circle based on its largest
    // dimension. This keeps valid gaps between bunkers and tanks open for
    // infantry.
    if (MovingPath.HasCollisionBounds)
    {
        Rect.Left += (float)MovingPath.CollisionRight + 1.0f;
        Rect.Top += (float)MovingPath.CollisionDown + 1.0f;
        Rect.Right += (float)MovingPath.CollisionLeft;
        Rect.Bottom += (float)MovingPath.CollisionUp;
    }
    else
    {
        const float Radius = (float)std::max(4, MovingPath.CollisionRadius);
        Rect.Left += Radius;
        Rect.Top += Radius;
        Rect.Right += Radius;
        Rect.Bottom += Radius;
    }
    return Rect;
}

static bool SegmentIntersectsStaticBlockerRect(
    const Core::Math::Vec2& Start,
    const Core::Math::Vec2& End,
    const StaticBlockerRect& Rect)
{
    // Runtime collision treats an exact edge touch as clear. Shrink the open
    // rectangle by a tiny epsilon so the path preflight uses the same rule.
    constexpr float kOpenEdgeEpsilon = 0.01f;
    const float MinX = Rect.Position.X - Rect.Left + kOpenEdgeEpsilon;
    const float MaxX = Rect.Position.X + Rect.Right - kOpenEdgeEpsilon;
    const float MinY = Rect.Position.Y - Rect.Top + kOpenEdgeEpsilon;
    const float MaxY = Rect.Position.Y + Rect.Bottom - kOpenEdgeEpsilon;
    if (MinX >= MaxX || MinY >= MaxY)
        return false;

    float Enter = 0.0f;
    float Exit = 1.0f;
    const Core::Math::Vec2 Delta = End - Start;
    const auto ClipAxis = [&](float Origin, float Direction,
                              float Minimum, float Maximum)
    {
        if (std::fabs(Direction) <= 0.0001f)
            return Origin > Minimum && Origin < Maximum;

        float Near = (Minimum - Origin) / Direction;
        float Far = (Maximum - Origin) / Direction;
        if (Near > Far)
            std::swap(Near, Far);
        Enter = std::max(Enter, Near);
        Exit = std::min(Exit, Far);
        return Enter <= Exit;
    };

    if (!ClipAxis(Start.X, Delta.X, MinX, MaxX) ||
        !ClipAxis(Start.Y, Delta.Y, MinY, MaxY))
    {
        return false;
    }

    // A segment beginning exactly on an edge and moving away has Exit == 0;
    // it is escaping a contact, not entering the blocker.
    return Exit > 0.0001f && Enter < 1.0f;
}

static bool PointInsideStaticBlockerRect(const Core::Math::Vec2& Point,
                                         const StaticBlockerRect& Rect)
{
    constexpr float kOpenEdgeEpsilon = 0.01f;
    return Point.X > Rect.Position.X - Rect.Left + kOpenEdgeEpsilon &&
           Point.X < Rect.Position.X + Rect.Right - kOpenEdgeEpsilon &&
           Point.Y > Rect.Position.Y - Rect.Top + kOpenEdgeEpsilon &&
           Point.Y < Rect.Position.Y + Rect.Bottom - kOpenEdgeEpsilon;
}

static Core::ECS::EntityID FindExactStaticBlockerOnPath(
    Core::ECS::EntityManager& EM,
    Core::ECS::EntityID MovingID,
    const Core::Math::Vec2& Start,
    const std::vector<Core::Math::Vec2>& Waypoints,
    const PathfindingComponent& MovingPath,
    Core::ECS::EntityID IgnoreEntityID)
{
    if (Waypoints.empty())
        return -1;

    const auto Entities = EM.GetEntitiesWith<TransformComponent, UnitComponent>();
    Core::Math::Vec2 SegmentStart = Start;
    for (const auto& Waypoint : Waypoints)
    {
        for (auto BlockerID : Entities)
        {
            if (BlockerID == MovingID || BlockerID == IgnoreEntityID ||
                !IsStaticGroundBlocker(EM, BlockerID))
            {
                continue;
            }

            const auto Rect = ExpandedStaticBlockerRectForMover(
                EM, BlockerID, MovingPath);
            // Harvest contact can intentionally leave a worker slightly inside
            // a mineral/geyser DAT rectangle. Once that job is interrupted,
            // the first construction or move route must be allowed to leave
            // the overlap. Re-entry is still caught on every later segment.
            if (PointInsideStaticBlockerRect(SegmentStart, Rect))
                continue;
            if (SegmentIntersectsStaticBlockerRect(
                    SegmentStart, Waypoint, Rect))
            {
                return BlockerID;
            }
        }
        SegmentStart = Waypoint;
    }
    return -1;
}

static std::function<bool(int, int)> BuildStaticBlockerTilePredicate(
    Core::ECS::EntityManager& EM,
    Core::ECS::EntityID MovingID,
    const Game::Map::GameMap& Map,
    int StartTileX,
    int StartTileY,
    const PathfindingComponent& MovingPath,
    int IgnoreEntityID = -1,
    int AvoidDynamicEntityID = -1)
{
    const auto* MovingPlayer = EM.GetComponent<PlayerComponent>(MovingID);
    const bool RequestedAvoidIsStatic =
        AvoidDynamicEntityID >= 0 &&
        IsStaticGroundBlocker(EM, AvoidDynamicEntityID);
    std::vector<StaticBlockerRect> Blockers;
    for (auto EntityID : EM.GetEntitiesWith<TransformComponent, UnitComponent>())
    {
        // The interaction target is normally omitted so A* can approach its
        // footprint.  Movement can pass that same entity back as the explicit
        // avoid target after exact pixel collision rejected the planned
        // segment.  In that recovery request the physical blocker must win;
        // keeping the planning ignore would reproduce the identical route.
        if (EntityID == IgnoreEntityID && EntityID != AvoidDynamicEntityID)
            continue;
        if (EntityID == MovingID)
            continue;
        const bool IsStaticBlocker = IsStaticGroundBlocker(EM, EntityID);
        const bool IsRequestedDynamicAvoid = EntityID == AvoidDynamicEntityID;
        const auto* BlockerPlayer = EM.GetComponent<PlayerComponent>(EntityID);
        const auto* BlockerPath = EM.GetComponent<PathfindingComponent>(EntityID);
        const bool IsStoppedFriendlyDynamicAvoid =
            AvoidDynamicEntityID >= 0 &&
            !RequestedAvoidIsStatic &&
            !IsStaticBlocker &&
            MovingPlayer && BlockerPlayer &&
            MovingPlayer->PlayerID == BlockerPlayer->PlayerID &&
            BlockerPath && BlockerPath->CurrentVelocity.LengthSq() <= 0.01f;
        if (!IsStaticBlocker &&
            !IsRequestedDynamicAvoid &&
            !IsStoppedFriendlyDynamicAvoid)
        {
            continue;
        }
        auto Rect = ExpandedStaticBlockerRectForMover(
            EM, EntityID, MovingPath);

        if (IsStaticBlocker && EntityID == AvoidDynamicEntityID)
        {
            // A* samples 32-pixel build-tile centers.  An exact DAT rectangle
            // can cross a tile near its edge while every sampled center stays
            // clear (for example y=2772 versus a row center at y=2768).  Add
            // half a tile only for the blocker proven by runtime collision so
            // the replacement route cannot smooth back through that sliver.
            constexpr float kExactCollisionRasterPadding = 16.0f;
            Rect.Left += kExactCollisionRasterPadding;
            Rect.Top += kExactCollisionRasterPadding;
            Rect.Right += kExactCollisionRasterPadding;
            Rect.Bottom += kExactCollisionRasterPadding;
        }
        Blockers.push_back(Rect);
    }

    const Game::Map::GameMap* MapPtr = &Map;
    return [Blockers = std::move(Blockers), MapPtr, StartTileX, StartTileY](int TileX, int TileY)
    {
        if (TileX == StartTileX && TileY == StartTileY)
            return false;

        if (!MapPtr)
            return false;

        const Core::Math::Vec2 Center = MapPtr->TileToWorld(TileX, TileY);
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
}

static Core::Math::Vec2 LimitPathStageToLocalSearch(
    const Game::Map::GameMap& Map,
    int StartTileX,
    int StartTileY,
    const Core::Math::Vec2& Candidate)
{
    constexpr int kMaxStageTileSpan = 20;
    int CandidateTileX = 0;
    int CandidateTileY = 0;
    Map.WorldToTile(Candidate, CandidateTileX, CandidateTileY);
    const int DeltaX = CandidateTileX - StartTileX;
    const int DeltaY = CandidateTileY - StartTileY;
    const int Span = std::max(std::abs(DeltaX), std::abs(DeltaY));
    if (Span <= kMaxStageTileSpan)
        return Candidate;

    const float Scale = (float)kMaxStageTileSpan / (float)Span;
    const int StageTileX = std::clamp(
        (int)std::lround((float)StartTileX + (float)DeltaX * Scale),
        0, Map.GetWidth() - 1);
    const int StageTileY = std::clamp(
        (int)std::lround((float)StartTileY + (float)DeltaY * Scale),
        0, Map.GetHeight() - 1);
    return Map.TileToWorld(StageTileX, StageTileY);
}

bool CommandSystem::RequestMovementPath(Core::ECS::EntityManager& EM,
                                        Core::ECS::EntityID EntityID,
                                        const Game::Map::GameMap* Map,
                                        const Core::Math::Vec2& Target,
                                        int PathingIgnoreEntityID,
                                        int AvoidDynamicEntityID)
{
    auto* Trans = EM.GetComponent<TransformComponent>(EntityID);
    auto* Path = EM.GetComponent<PathfindingComponent>(EntityID);
    if (!Trans || !Path)
        return false;

    const bool DestinationChanged =
        Path->RequestedDestination.DistanceTo(Target) > 1.0f ||
        Path->RequestedIgnoreEntityID != PathingIgnoreEntityID;
    Path->RequestedDestination = Target;
    Path->RequestedIgnoreEntityID = PathingIgnoreEntityID;
    Path->Path.clear();
    Path->PathIndex = 0;
    Path->BlockedByEntityID = -1;
    if (DestinationChanged)
    {
        Path->DestinationStallTimer = 0.0f;
        Path->HasLastDestinationDistance = false;
        if (!Path->HasRecentMoveFailure ||
            Path->RecentFailedMoveTarget.DistanceTo(Target) > 16.0f)
        {
            Path->HasRecentMoveFailure = false;
            Path->RecentMoveFailureCooldown = 0.0f;
        }
    }

    if (Path->IsAir || !Map)
    {
        Path->Path.push_back(Target);
        return true;
    }

    if (sRegionPathfinderMap != Map)
        RebuildRegionPathfinder(Map);

    int StartTileX = 0;
    int StartTileY = 0;
    int EndTileX = 0;
    int EndTileY = 0;
    Map->WorldToTile(Trans->Position, StartTileX, StartTileY);
    Map->WorldToTile(Target, EndTileX, EndTileY);
    const auto ExtraBlockedTile = BuildStaticBlockerTilePredicate(
        EM,
        EntityID,
        *Map,
        StartTileX,
        StartTileY,
        *Path,
        PathingIgnoreEntityID,
        AvoidDynamicEntityID);

    Core::Math::Vec2 StageTarget = Target;
    const auto RegionPath = sRegionPathfinder.FindRegionPath(
        StartTileX, StartTileY, EndTileX, EndTileY);
    if (RegionPath.Found && RegionPath.RegionSequence.size() > 1)
    {
        const int NextRegionID = RegionPath.RegionSequence[1];
        if (const auto* NextRegion = sRegionPathfinder.GetRegion(NextRegionID))
        {
            StageTarget = Map->TileToWorld(
                NextRegion->CenterX, NextRegion->CenterY);
        }
    }

    // A region center can still be farther away than one bounded tile search,
    // and simple connected maps may collapse to a single large region. In both
    // cases keep the local A* leg short and request the next leg on arrival.
    StageTarget = LimitPathStageToLocalSearch(
        *Map, StartTileX, StartTileY, StageTarget);

    const auto FindRoute = [&](const std::function<bool(int, int)>& BlockedTile,
                               bool& OutUsedExpandedFallback)
    {
        OutUsedExpandedFallback = false;
        auto Candidate = Game::Pathfinding::AStar::FindPath(
            *Map,
            Trans->Position,
            StageTarget,
            1000,
            TerrainClearanceForPath(*Path),
            BlockedTile);
        if (!Candidate.Found || Candidate.Waypoints.empty())
        {
            // A local stage can sit beyond a large wall or choke whose detour
            // alone exceeds the short-search budget. Resolve that exceptional
            // route once with a map-sized budget, then keep only a reachable
            // local prefix when possible. This avoids repeating the identical
            // 1000-node failure.
            const int ExpandedNodeBudget = std::max(
                1000, Map->GetWidth() * Map->GetHeight());
            auto Expanded = Game::Pathfinding::AStar::FindPath(
                *Map,
                Trans->Position,
                Target,
                ExpandedNodeBudget,
                TerrainClearanceForPath(*Path),
                BlockedTile);
            if (Expanded.Found && !Expanded.Waypoints.empty())
            {
                Core::Math::Vec2 PrefixTarget = LimitPathStageToLocalSearch(
                    *Map, StartTileX, StartTileY, Expanded.Waypoints.front());
                auto Prefix = Game::Pathfinding::AStar::FindPath(
                    *Map,
                    Trans->Position,
                    PrefixTarget,
                    1000,
                    TerrainClearanceForPath(*Path),
                    BlockedTile);
                if (Prefix.Found && !Prefix.Waypoints.empty())
                {
                    StageTarget = PrefixTarget;
                    Candidate = std::move(Prefix);
                }
                else
                {
                    // The expanded route itself is known reachable, so retain
                    // it rather than blacklist a valid final destination.
                    Candidate = std::move(Expanded);
                }
                OutUsedExpandedFallback = true;
            }
        }
        return Candidate;
    };

    bool UsedExpandedFallback = false;
    auto Result = FindRoute(ExtraBlockedTile, UsedExpandedFallback);
    Core::ECS::EntityID ExactPreflightBlockerID = -1;
    if (AvoidDynamicEntityID < 0 && Result.Found && !Result.Waypoints.empty())
    {
        ExactPreflightBlockerID = FindExactStaticBlockerOnPath(
            EM,
            EntityID,
            Trans->Position,
            Result.Waypoints,
            *Path,
            PathingIgnoreEntityID);
        if (ExactPreflightBlockerID >= 0)
        {
            // Tile-center rasterization can miss a thin DAT collision sliver.
            // Before motion begins, rebuild only around the exact static entity
            // proven by segment preflight. The command's interaction ignore is
            // still passed separately and remains unchanged.
            const auto StrictBlockedTile = BuildStaticBlockerTilePredicate(
                EM,
                EntityID,
                *Map,
                StartTileX,
                StartTileY,
                *Path,
                PathingIgnoreEntityID,
                ExactPreflightBlockerID);
            bool StrictUsedExpandedFallback = false;
            auto StrictResult = FindRoute(
                StrictBlockedTile, StrictUsedExpandedFallback);
            if (StrictResult.Found && !StrictResult.Waypoints.empty())
            {
                Result = std::move(StrictResult);
                UsedExpandedFallback = StrictUsedExpandedFallback;
            }
            else
            {
                // Do not commit the known-colliding route when the strict
                // raster has no exit from the current tile. Preserve the exact
                // blocker so HarvestSystem can adopt a live blocking mineral
                // (or another order can report a genuine path failure) instead
                // of immediately issuing the identical straight route again.
                Result = std::move(StrictResult);
                Path->BlockedByEntityID = ExactPreflightBlockerID;
                Path->HasRecentMoveFailure = true;
                Path->RecentFailedMoveTarget = Target;
                Path->RecentMoveFailureCooldown = 1.0f;
                Path->CurrentSpeed = 0.0f;
                Path->TargetSpeed = 0.0f;
                Path->CurrentVelocity = {0.0f, 0.0f};
                UsedExpandedFallback = StrictUsedExpandedFallback;
            }
        }
    }
    if (Result.Found && !Result.Waypoints.empty())
        Path->Path = std::move(Result.Waypoints);

    if (PathingDebugEnabled())
    {
        const auto* Unit = EM.GetComponent<UnitComponent>(EntityID);
        Core::Utils::Logger::Info("Pathing",
            "request entity=" + std::to_string(EntityID) +
            " unit=" + std::to_string(Unit ? Unit->ChkUnitID : -1) +
            " start=" + std::to_string((int)Trans->Position.X) + "," +
                std::to_string((int)Trans->Position.Y) +
            " target=" + std::to_string((int)Target.X) + "," +
                std::to_string((int)Target.Y) +
            " stage=" + std::to_string((int)StageTarget.X) + "," +
                std::to_string((int)StageTarget.Y) +
            " clearance=" + std::to_string(TerrainClearanceForPath(*Path)) +
            " preflight_blocker=" + std::to_string(ExactPreflightBlockerID) +
            " expanded=" + std::to_string(UsedExpandedFallback ? 1 : 0) +
            " found=" + std::to_string(Result.Found ? 1 : 0) +
            " nodes=" + std::to_string(Path->Path.size()));
    }
    return !Path->Path.empty();
}

static void ConfigureMovementPathForOrder(Core::ECS::EntityManager& EM,
                                          Core::ECS::EntityID EntityID,
                                          const Order& InOrder)
{
    if (!IsMovementOrder(InOrder.Type))
        return;

    auto* Path = EM.GetComponent<PathfindingComponent>(EntityID);
    if (!Path)
        Path = &EM.AddComponent<PathfindingComponent>(EntityID);
    Path->Path.clear();
    Path->PathIndex = 0;
    Path->StallTimer = 0.0f;
    Path->HasLastWaypointDistance = false;
    Path->DestinationStallTimer = 0.0f;
    Path->HasLastDestinationDistance = false;
    Path->RequestedDestination = InOrder.TargetPosition;
    Path->RequestedTargetEntityID = InOrder.TargetEntityID;
    Path->RequestedIgnoreEntityID = InOrder.PathingIgnoreEntityID;
    Path->LastRepathTime = 0.0f;
    Path->RepathFailCount = 0;
    Path->BlockedByEntityID = -1;
    if (!Path->HasRecentMoveFailure ||
        (Path->RecentFailedMoveTarget - InOrder.TargetPosition).LengthSq() > 16.0f * 16.0f)
    {
        Path->HasRecentMoveFailure = false;
        Path->RecentMoveFailureCooldown = 0.0f;
    }
    Path->NodeTolerance = 8.0f;
    RefreshCollisionRadius(EM, EntityID, *Path);
    Path->IsAir = ShouldMoveAsAir(EM, EntityID);
    if (Path->HasRecentMoveFailure &&
        (Path->RecentFailedMoveTarget - InOrder.TargetPosition).LengthSq() <= 16.0f * 16.0f)
    {
        Path->CurrentSpeed = 0.0f;
        Path->TargetSpeed = 0.0f;
        Path->CurrentVelocity = {0.0f, 0.0f};
        if (PathingDebugEnabled())
            Core::Utils::Logger::Info("Pathing",
                "order suppressed entity=" + std::to_string(EntityID) +
                " target=" + std::to_string((int)InOrder.TargetPosition.X) + "," +
                    std::to_string((int)InOrder.TargetPosition.Y));
        return;
    }

    CommandSystem::RequestMovementPath(
        EM,
        EntityID,
        gCmdMap,
        InOrder.TargetPosition,
        InOrder.PathingIgnoreEntityID);
}

static float DistanceToRepairFootprint(Core::ECS::EntityManager& EM,
                                       const Core::Math::Vec2& WorkerPosition,
                                       Core::ECS::EntityID TargetEntityID)
{
    const auto* TargetTransform = EM.GetComponent<TransformComponent>(TargetEntityID);
    if (!TargetTransform)
        return std::numeric_limits<float>::max();

    float Left = 48.0f;
    float Up = 32.0f;
    float Right = 48.0f;
    float Down = 32.0f;
    if (const auto* TargetPath = EM.GetComponent<PathfindingComponent>(TargetEntityID))
    {
        if (TargetPath->HasCollisionBounds)
        {
            Left = (float)TargetPath->CollisionLeft;
            Up = (float)TargetPath->CollisionUp;
            Right = (float)TargetPath->CollisionRight + 1.0f;
            Down = (float)TargetPath->CollisionDown + 1.0f;
        }
        else if (TargetPath->CollisionRadius > 0)
        {
            Left = Up = Right = Down = (float)TargetPath->CollisionRadius;
        }
    }

    const Core::Math::Vec2 Delta = WorkerPosition - TargetTransform->Position;
    const Core::Math::Vec2 Nearest{
        TargetTransform->Position.X + std::clamp(Delta.X, -Left, Right),
        TargetTransform->Position.Y + std::clamp(Delta.Y, -Up, Down)
    };
    return WorkerPosition.DistanceTo(Nearest);
}

static bool IsTerranRepairableMechanicalUnit(const UnitComponent& Unit)
{
    if (Unit.UnitRace != Race::Terran)
        return false;
    const int DefID = Unit.ChkUnitID == 30
        ? Game::Races::GetUnitDefID("SiegeTank")
        : Game::Races::GetUnitDefIDByChkUnitID(Unit.ChkUnitID);
    if (DefID < 0)
        return false;
    const auto Def = Game::Races::GetUnitDefByID(DefID);
    if (Def.IsBuilding)
        return false;

    return Def.Name == "SCV" ||
           Def.Name == "Vulture" ||
           Def.Name == "Goliath" ||
           Def.Name == "SiegeTank" ||
           Def.Name == "Wraith" ||
           Def.Name == "Dropship" ||
           Def.Name == "ScienceVessel" ||
           Def.Name == "Battlecruiser" ||
           Def.Name == "Valkyrie" ||
           Def.Name == "JimRaynorVulture" ||
           Def.Name == "EdmundDukeSiege" ||
           Def.Name == "ArcturusMengsk" ||
           Def.Name == "Hyperion" ||
           Def.Name == "NoradII";
}

void CommandSystem::Update(float DeltaTime)
{
    if (DeltaTime <= 0.0f || !gCmdMap) return;

    auto& EM = mSystemMgr->GetEntityManager();
    auto Units = EM.GetEntitiesWith<CommandComponent, TransformComponent>();

    for (int ID : Units)
    {
        auto* Cmd = EM.GetComponent<CommandComponent>(ID);
        if (!Cmd || !Cmd->HasPendingOrders()) continue;

        const Order* Order = Cmd->GetCurrentOrder();
        if (!Order) continue;

        switch (Order->Type)
        {
            case OrderType::Burrow:
            case OrderType::Unburrow:
            {
                auto* Burrow = EM.GetComponent<BurrowComponent>(ID);
                if (!Burrow || !Burrow->CanBurrow)
                {
                    AdvanceQueue(EM, ID);
                    break;
                }

                const bool ToBurrowed = Order->Type == OrderType::Burrow;
                const bool FullyBurrowed =
                    Burrow->IsBurrowed || Burrow->Phase == BurrowPhase::Burrowed;
                if (!Burrow->IsTransitioning() && ToBurrowed == FullyBurrowed)
                {
                    AdvanceQueue(EM, ID);
                    break;
                }
                if (!Burrow->IsTransitioning())
                    BurrowState::BeginTransition(EM, ID, ToBurrowed);
                break;
            }

            case OrderType::Guard:
            {
                auto* Combat = EM.GetComponent<CombatComponent>(ID);
                if (!Combat || !ShouldAutoAttackOnGuard(*Combat)) break;

                // Guard = hold position but auto-acquire targets
                // Find nearest enemy in weapon range
                int EnemyID = -1;
                float BestDist = Combat->AttackRange * 32.0f * 2.0f;
                auto* MyTrans = EM.GetComponent<TransformComponent>(ID);
                if (!MyTrans) break;

                auto Enemies = EM.GetEntitiesWith<TransformComponent, UnitComponent, PlayerComponent>();
                for (int Eid : Enemies)
                {
                    auto* EUnit = EM.GetComponent<UnitComponent>(Eid);
                    auto* EPlayer = EM.GetComponent<PlayerComponent>(Eid);
                    auto* ETrans = EM.GetComponent<TransformComponent>(Eid);
                    auto* MyPlayer = EM.GetComponent<PlayerComponent>(ID);
                    if (!EUnit || !EPlayer || !ETrans || !MyPlayer) continue;
                    if (EUnit->HitPoints <= 0) continue;
                    if (EPlayer->PlayerID == MyPlayer->PlayerID) continue;

                    float D = MyTrans->Position.DistanceTo(ETrans->Position);
                    if (D < BestDist)
                    {
                        BestDist = D;
                        EnemyID = Eid;
                    }
                }

                if (EnemyID >= 0)
                {
                    CombatSystem::IssueAttack(EM, {ID}, EnemyID);
                }
                break;
            }

            case OrderType::Stop:
            {
                auto* Path = EM.GetComponent<PathfindingComponent>(ID);
                if (Path) { Path->Path.clear(); Path->PathIndex = 0; }
                Cmd->CurrentCommand = CommandType::Stop;
                Cmd->TargetEntityID = -1;
                break;
            }

            case OrderType::Repair:
            {
                bool ContinueRepair = false;
                if (Order->TargetEntityID >= 0)
                {
                    auto* TargetUnit = EM.GetComponent<UnitComponent>(Order->TargetEntityID);
                    auto* TargetBuilding = EM.GetComponent<BuildingComponent>(Order->TargetEntityID);
                    auto* TargetPlayer = EM.GetComponent<PlayerComponent>(Order->TargetEntityID);
                    auto* MyPlayer = EM.GetComponent<PlayerComponent>(ID);
                    auto* MyUnit = EM.GetComponent<UnitComponent>(ID);
                    auto* MyTrans = EM.GetComponent<TransformComponent>(ID);
                    auto* TargetTrans =
                        EM.GetComponent<TransformComponent>(Order->TargetEntityID);

                    const bool RepairableTarget = TargetUnit &&
                        ((TargetBuilding && !TargetBuilding->UnderConstruction) ||
                         (!TargetBuilding &&
                          IsTerranRepairableMechanicalUnit(*TargetUnit)));
                    if (RepairableTarget && TargetPlayer && MyPlayer &&
                        MyUnit && MyUnit->ChkUnitID == 7 && MyTrans && TargetTrans &&
                        TargetPlayer->PlayerID == MyPlayer->PlayerID &&
                        TargetUnit->UnitRace == Components::Race::Terran &&
                        TargetUnit->HitPoints > 0 &&
                        (TargetUnit->HitPoints < TargetUnit->MaxHitPoints ||
                         TargetUnit->HitPointSubDamage > 0))
                    {
                        ContinueRepair = true;
                        if (MyUnit->RepairTargetEntityID != Order->TargetEntityID)
                        {
                            MyUnit->RepairTargetEntityID = Order->TargetEntityID;
                            MyUnit->RepairHitPointAccumulator = 0.0f;
                            // Mineral/gas fractions are already-consumed repair
                            // value. Keep that debt on the SCV across targets so
                            // cancelling or repeatedly switching after one HP
                            // cannot turn fractional repair costs into free work.
                        }
                        const auto* MyPath = EM.GetComponent<PathfindingComponent>(ID);
                        const float WorkerRadius = MyPath
                            ? (float)std::max(4, MyPath->CollisionRadius)
                            : 16.0f;
                        const float Distance = DistanceToRepairFootprint(
                            EM, MyTrans->Position, Order->TargetEntityID);
                        if (Distance <= WorkerRadius + 12.0f)
                        {
                            if (auto* Path = EM.GetComponent<PathfindingComponent>(ID))
                            {
                                Path->RequestedDestination = TargetTrans->Position;
                                Path->RequestedTargetEntityID = Order->TargetEntityID;
                                Path->RequestedIgnoreEntityID = Order->PathingIgnoreEntityID;
                                Path->Path.clear();
                                Path->PathIndex = 0;
                                Path->CurrentSpeed = 0.0f;
                                Path->TargetSpeed = 0.0f;
                                Path->CurrentVelocity = {0.0f, 0.0f};
                            }
                            int TargetDefID = TargetBuilding &&
                                TargetBuilding->BuildingDefID >= 0
                                ? TargetBuilding->BuildingDefID
                                : (TargetUnit->ChkUnitID == 30
                                    ? Game::Races::GetUnitDefID("SiegeTank")
                                    : Game::Races::GetUnitDefIDByChkUnitID(
                                        TargetUnit->ChkUnitID));
                            const auto TargetDef =
                                Game::Races::GetUnitDefByID(TargetDefID);
                            const float BuildSeconds =
                                (float)std::max(1, TargetDef.BuildTime);
                            // New units begin construction at 10% HP in BW;
                            // repair advances by that same 90%-over-build-time
                            // rate, independent of render/update frame rate.
                            const float RepairRate = std::max(
                                24.0f / 256.0f,
                                0.9f * (float)TargetUnit->MaxHitPoints /
                                    BuildSeconds);
                            MyUnit->RepairHitPointAccumulator +=
                                RepairRate * DeltaTime;

                            auto& Resources = HarvestSystem::GetResources(
                                MyPlayer->PlayerID);
                            const float MineralCostPerPoint =
                                TargetUnit->MaxHitPoints > 0
                                ? (float)TargetDef.MineralCost /
                                    (3.0f * (float)TargetUnit->MaxHitPoints)
                                : 0.0f;
                            const float GasCostPerPoint =
                                TargetUnit->MaxHitPoints > 0
                                ? (float)TargetDef.GasCost /
                                    (3.0f * (float)TargetUnit->MaxHitPoints)
                                : 0.0f;

                            int RequestedPoints =
                                (int)MyUnit->RepairHitPointAccumulator;
                            int RepairedPoints = 0;
                            while (RequestedPoints-- > 0 &&
                                   (TargetUnit->HitPoints <
                                        TargetUnit->MaxHitPoints ||
                                    TargetUnit->HitPointSubDamage > 0))
                            {
                                if ((MineralCostPerPoint > 0.0f &&
                                     Resources.Minerals <= 0) ||
                                    (GasCostPerPoint > 0.0f &&
                                     Resources.VespeneGas <= 0))
                                {
                                    ContinueRepair = false;
                                    break;
                                }

                                const float NextMineral =
                                    MyUnit->RepairMineralCostAccumulator +
                                    MineralCostPerPoint;
                                const float NextGas =
                                    MyUnit->RepairGasCostAccumulator +
                                    GasCostPerPoint;
                                const int MineralCharge = (int)NextMineral;
                                const int GasCharge = (int)NextGas;
                                if (Resources.Minerals < MineralCharge ||
                                    Resources.VespeneGas < GasCharge)
                                {
                                    ContinueRepair = false;
                                    break;
                                }

                                Resources.Minerals -= MineralCharge;
                                Resources.VespeneGas -= GasCharge;
                                MyUnit->RepairMineralCostAccumulator =
                                    NextMineral - (float)MineralCharge;
                                MyUnit->RepairGasCostAccumulator =
                                    NextGas - (float)GasCharge;
                                TargetUnit->HitPoints = std::min(
                                    TargetUnit->MaxHitPoints,
                                    TargetUnit->HitPoints + 1);
                                TargetUnit->HitPointSubDamage = 0;
                                ++RepairedPoints;
                            }
                            MyUnit->RepairHitPointAccumulator -=
                                (float)RepairedPoints;
                            MyUnit->RepairHitPointAccumulator = std::max(
                                0.0f, MyUnit->RepairHitPointAccumulator);

                            if (TargetUnit->HitPoints >=
                                    TargetUnit->MaxHitPoints &&
                                TargetUnit->HitPointSubDamage == 0)
                            {
                                ContinueRepair = false;
                            }
                        }
                        else if (auto* Path = EM.GetComponent<PathfindingComponent>(ID))
                        {
                            const bool TargetMoved =
                                Path->RequestedDestination.DistanceTo(TargetTrans->Position) >
                                    std::max(8.0f, WorkerRadius);
                            const bool PathEnded =
                                Path->Path.empty() ||
                                Path->PathIndex >= (int)Path->Path.size();

                            Cmd->TargetPosition = TargetTrans->Position;
                            if (Cmd->CurrentOrderIndex >= 0 &&
                                Cmd->CurrentOrderIndex < (int)Cmd->Orders.size())
                            {
                                auto& ActiveOrder = Cmd->Orders[Cmd->CurrentOrderIndex];
                                ActiveOrder.TargetPosition = TargetTrans->Position;
                            }
                            Path->RequestedTargetEntityID = Order->TargetEntityID;

                            if (TargetMoved ||
                                (PathEnded && Path->LastRepathTime >= 0.25f))
                            {
                                RequestMovementPath(
                                    EM,
                                    ID,
                                    gCmdMap,
                                    TargetTrans->Position,
                                    Order->PathingIgnoreEntityID);
                                Path->RequestedTargetEntityID = Order->TargetEntityID;
                                Path->LastRepathTime = 0.0f;
                            }
                        }
                    }
                }
                if (!ContinueRepair)
                {
                    if (auto* Repairer = EM.GetComponent<UnitComponent>(ID))
                    {
                        Repairer->RepairTargetEntityID = -1;
                        Repairer->RepairHitPointAccumulator = 0.0f;
                        // Resource fractions remain unsettled until a later
                        // repair crosses an integer mineral/gas boundary.
                    }
                    AdvanceQueue(EM, ID);
                }
                break;
            }

            default: break;
        }
    }
}

// --- Order interface ---

void CommandSystem::IssueOrder(Core::ECS::EntityManager& EM,
                                Core::ECS::EntityID EntityID,
                                const Order& InOrder,
                                bool Queued)
{
    if (IsMovementOrder(InOrder.Type) && !CanReceiveMovementOrder(EM, EntityID))
    {
        if (Queued && CanQueueMovementAfterUnburrow(EM, EntityID))
        {
            auto* Command = EM.GetComponent<CommandComponent>(EntityID);
            if (!Command)
                Command = &EM.AddComponent<CommandComponent>(EntityID);

            const Order* CurrentOrder = Command->GetCurrentOrder();
            if (!CurrentOrder || CurrentOrder->Type != OrderType::Unburrow)
            {
                // A transition started directly through BurrowState has no rich
                // order to act as a queue barrier. Install one so the queued
                // movement becomes active only after the surface signal.
                Command->Orders.clear();
                Command->CurrentOrderIndex = -1;
                Order TransitionBarrier;
                TransitionBarrier.Type = OrderType::Unburrow;
                Command->QueueOrder(TransitionBarrier);
            }
            Command->Orders.push_back(InOrder);
            return;
        }
        if (!IsFrozenByStatus(EM, EntityID))
            ClearMovementPath(EM, EntityID);
        return;
    }

    auto* Cmd = EM.GetComponent<CommandComponent>(EntityID);
    if (!Cmd)
        Cmd = &EM.AddComponent<CommandComponent>(EntityID);

    if (Queued && Cmd->HasPendingOrders())
    {
        Cmd->Orders.push_back(InOrder);
        return;
    }

    Cmd->Orders.clear();
    Cmd->CurrentOrderIndex = -1;
    Cmd->QueueOrder(InOrder);

    if (InOrder.Type == OrderType::UseTech)
        TryApplyUseTechOrder(EM, EntityID, InOrder);

    ConfigureMovementPathForOrder(EM, EntityID, InOrder);
}

void CommandSystem::IssueOrder(Core::ECS::EntityManager& EM,
                                const std::vector<Core::ECS::EntityID>& Entities,
                                const Order& InOrder,
                                bool Queued)
{
    struct GroupMove
    {
        struct MemberFootprint
        {
            Core::Math::Vec2 Position;
            float Left = 0.0f;
            float Up = 0.0f;
            float Right = 0.0f;
            float Down = 0.0f;
        };

        Core::Math::Vec2 Center;
        Core::Math::Vec2 Min;
        Core::Math::Vec2 Max;
        std::vector<MemberFootprint> Members;
        int Count = 0;
        bool AnyGroundUnit = false;
        bool TargetInsideBounds = false;
        bool HasOverlappingMembers = false;
        bool PreserveOffsets = false;
        bool CompressInfantryOffsets = false;
        float SlotSpacingX = 24.0f;
        float SlotSpacingY = 24.0f;
        int SlotColumns = 1;
        float ArrivalTolerance = 0.0f;
    } Group;

    const bool AdjustGroupPositions =
        Entities.size() >= 2 &&
        InOrder.TargetEntityID < 0 &&
        IsGroupPositionOrder(InOrder.Type);

    if (AdjustGroupPositions)
    {
        for (Core::ECS::EntityID ID : Entities)
        {
            if (!CanReceiveMovementOrder(EM, ID))
                continue;
            auto* Trans = EM.GetComponent<TransformComponent>(ID);
            if (!Trans)
                continue;

            if (Group.Count == 0)
            {
                Group.Min = Trans->Position;
                Group.Max = Trans->Position;
            }
            else
            {
                Group.Min.X = std::min(Group.Min.X, Trans->Position.X);
                Group.Min.Y = std::min(Group.Min.Y, Trans->Position.Y);
                Group.Max.X = std::max(Group.Max.X, Trans->Position.X);
                Group.Max.Y = std::max(Group.Max.Y, Trans->Position.Y);
            }
            Group.Center += Trans->Position;
            ++Group.Count;
            Group.AnyGroundUnit = Group.AnyGroundUnit || !ShouldMoveAsAir(EM, ID);
            float Left = (float)CollisionRadiusForUnit(
                EM.GetComponent<UnitComponent>(ID));
            float Up = Left;
            float Right = Left;
            float Down = Left;
            if (auto* Path = EM.GetComponent<PathfindingComponent>(ID);
                Path && Path->HasCollisionBounds)
            {
                Left = (float)Path->CollisionLeft;
                Up = (float)Path->CollisionUp;
                Right = (float)Path->CollisionRight + 1.0f;
                Down = (float)Path->CollisionDown + 1.0f;
                const float Width = Left + Right;
                const float Height = Up + Down;
                Group.SlotSpacingX = std::max(Group.SlotSpacingX, Width + 4.0f);
                Group.SlotSpacingY = std::max(Group.SlotSpacingY, Height + 4.0f);
            }
            Group.Members.push_back({Trans->Position, Left, Up, Right, Down});
        }

        if (Group.Count >= 2)
        {
            Group.Center = Group.Center / (float)Group.Count;
            for (size_t I = 0;
                 I < Group.Members.size() && !Group.HasOverlappingMembers;
                 ++I)
            {
                const auto& A = Group.Members[I];
                for (size_t J = I + 1; J < Group.Members.size(); ++J)
                {
                    const auto& B = Group.Members[J];
                    if (A.Position.X - A.Left < B.Position.X + B.Right &&
                        A.Position.X + A.Right > B.Position.X - B.Left &&
                        A.Position.Y - A.Up < B.Position.Y + B.Down &&
                        A.Position.Y + A.Down > B.Position.Y - B.Up)
                    {
                        Group.HasOverlappingMembers = true;
                        break;
                    }
                }
            }
            const float MaxSpan = Group.AnyGroundUnit ? 192.0f : 256.0f;
            const bool CompactSelection =
                Group.Max.X - Group.Min.X <= MaxSpan &&
                Group.Max.Y - Group.Min.Y <= MaxSpan;
            Group.TargetInsideBounds =
                CompactSelection &&
                InOrder.TargetPosition.X >= Group.Min.X &&
                InOrder.TargetPosition.X <= Group.Max.X &&
                InOrder.TargetPosition.Y >= Group.Min.Y &&
                InOrder.TargetPosition.Y <= Group.Max.Y;
            // Preserve a real formation, but do not preserve a spawn/rally
            // pile-up. Compressing already-overlapping offsets creates
            // overlapping destination slots and a permanent mutual block.
            Group.PreserveOffsets = CompactSelection &&
                !Group.TargetInsideBounds && !Group.HasOverlappingMembers;
            Group.CompressInfantryOffsets =
                Group.PreserveOffsets &&
                Group.SlotSpacingX <= 24.0f &&
                Group.SlotSpacingY <= 24.0f;
            while (Group.SlotColumns * Group.SlotColumns < Group.Count)
                ++Group.SlotColumns;
            if (!Group.TargetInsideBounds &&
                !Group.PreserveOffsets &&
                InOrder.Type == OrderType::AttackMove &&
                Group.Count >= 64)
            {
                const float CrowdGridSpacing = std::max(
                    {Group.SlotSpacingX, Group.SlotSpacingY, 64.0f});
                Group.ArrivalTolerance = CrowdGridSpacing * 2.0f;
            }
        }
    }

    int FormationSlot = 0;
    for (Core::ECS::EntityID ID : Entities)
    {
        Order EntityOrder = InOrder;
        if (AdjustGroupPositions && Group.Count >= 2 && CanReceiveMovementOrder(EM, ID))
        {
            if (auto* Trans = EM.GetComponent<TransformComponent>(ID))
            {
                if (Group.TargetInsideBounds)
                {
                    EntityOrder.TargetPosition.X = std::clamp(
                        Trans->Position.X,
                        InOrder.TargetPosition.X - 32.0f,
                        InOrder.TargetPosition.X + 32.0f);
                    EntityOrder.TargetPosition.Y = std::clamp(
                        Trans->Position.Y,
                        InOrder.TargetPosition.Y - 32.0f,
                        InOrder.TargetPosition.Y + 32.0f);
                }
                else if (Group.PreserveOffsets)
                {
                    constexpr float kInfantryFormationCompression = 0.80f;
                    const float Scale = Group.CompressInfantryOffsets
                        ? kInfantryFormationCompression
                        : 1.0f;
                    EntityOrder.TargetPosition =
                        InOrder.TargetPosition + (Trans->Position - Group.Center) * Scale;
                }
                else
                {
                    const float CrowdGridSpacing =
                        Group.Count >= 64 ? 64.0f : 48.0f;
                    const float GridSpacingX =
                        std::max(Group.SlotSpacingX, CrowdGridSpacing);
                    const float GridSpacingY =
                        std::max(Group.SlotSpacingY, CrowdGridSpacing);
                    const int Row = FormationSlot / Group.SlotColumns;
                    const int Column = FormationSlot % Group.SlotColumns;
                    const int Rows = (Group.Count + Group.SlotColumns - 1) /
                        Group.SlotColumns;
                    EntityOrder.TargetPosition = {
                        InOrder.TargetPosition.X +
                            ((float)Column - (float)(Group.SlotColumns - 1) * 0.5f) *
                                GridSpacingX,
                        InOrder.TargetPosition.Y +
                            ((float)Row - (float)(Rows - 1) * 0.5f) *
                                GridSpacingY
                    };
                }
                ++FormationSlot;
            }
        }

        if (gCmdMap)
        {
            float Left = (float)CollisionRadiusForUnit(EM.GetComponent<UnitComponent>(ID));
            float Up = Left;
            float Right = Left;
            float Down = Left;
            if (auto* Path = EM.GetComponent<PathfindingComponent>(ID);
                Path && Path->HasCollisionBounds)
            {
                Left = (float)Path->CollisionLeft;
                Up = (float)Path->CollisionUp;
                Right = (float)Path->CollisionRight + 1.0f;
                Down = (float)Path->CollisionDown + 1.0f;
            }

            const float MapWidth = (float)gCmdMap->GetWidth() * 32.0f;
            const float MapHeight = (float)gCmdMap->GetHeight() * 32.0f;
            EntityOrder.TargetPosition.X = std::clamp(
                EntityOrder.TargetPosition.X, Left, std::max(Left, MapWidth - Right));
            EntityOrder.TargetPosition.Y = std::clamp(
                EntityOrder.TargetPosition.Y, Up, std::max(Up, MapHeight - Down));
        }

        IssueOrder(EM, ID, EntityOrder, Queued);
        if (Group.ArrivalTolerance > 0.0f)
        {
            if (auto* Path = EM.GetComponent<PathfindingComponent>(ID))
                Path->NodeTolerance = std::max(
                    Path->NodeTolerance, Group.ArrivalTolerance);
        }
    }
}

void CommandSystem::IssueMove(Core::ECS::EntityManager& EM,
                               const std::vector<Core::ECS::EntityID>& Entities,
                               const Core::Math::Vec2& Target)
{
    IssueMove(EM, Entities, Target, -1);
}

void CommandSystem::IssueMove(Core::ECS::EntityManager& EM,
                               const std::vector<Core::ECS::EntityID>& Entities,
                               const Core::Math::Vec2& Target,
                               int PathingIgnoreEntityID)
{
    for (Core::ECS::EntityID EntityID : Entities)
        InterruptHarvest(EM, EntityID);

    Order MoveOrder;
    MoveOrder.Type = OrderType::Move;
    MoveOrder.TargetPosition = Target;
    MoveOrder.PathingIgnoreEntityID = PathingIgnoreEntityID;
    IssueOrder(EM, Entities, MoveOrder, false);
}

void CommandSystem::InterruptHarvest(Core::ECS::EntityManager& EM,
                                     Core::ECS::EntityID EntityID)
{
    auto* Harvest = EM.GetComponent<HarvestComponent>(EntityID);
    if (!Harvest ||
        (Harvest->State == HarvestState::Idle &&
         Harvest->TargetResourceID < 0 && Harvest->DesiredResourceID < 0))
        return;

    const auto ReleaseFromResource = [&](int ResourceID)
    {
        auto* Resource = EM.GetComponent<ResourceComponent>(ResourceID);
        if (!Resource)
            return;
        if (Resource->ActiveGathererEntityID == EntityID)
            Resource->ActiveGathererEntityID = -1;
        Resource->QueuedGatherers.erase(
            std::remove(Resource->QueuedGatherers.begin(),
                        Resource->QueuedGatherers.end(), EntityID),
            Resource->QueuedGatherers.end());
    };

    ReleaseFromResource(Harvest->TargetResourceID);
    if (Harvest->DesiredResourceID != Harvest->TargetResourceID)
        ReleaseFromResource(Harvest->DesiredResourceID);

    if (Harvest->InsideGasBuilding)
    {
        if (auto* Transform = EM.GetComponent<TransformComponent>(EntityID))
            Transform->Position = Harvest->GasExitPosition;
        if (auto* Render = EM.GetComponent<RenderComponent>(EntityID))
            Render->Visible = true;
        Harvest->InsideGasBuilding = false;
    }

    Harvest->State = HarvestState::Idle;
    Harvest->MineTimer = 0.0f;
    Harvest->TargetResourceID = -1;
    Harvest->DesiredResourceID = -1;
    Harvest->ReturnBuildingID = -1;
    Harvest->QueuedAtResource = false;
}

bool CommandSystem::IssueMoveAndHasPath(Core::ECS::EntityManager& EM,
                                        Core::ECS::EntityID EntityID,
                                        const Core::Math::Vec2& Target,
                                        int PathingIgnoreEntityID)
{
    Order MoveOrder;
    MoveOrder.Type = OrderType::Move;
    MoveOrder.TargetPosition = Target;
    MoveOrder.PathingIgnoreEntityID = PathingIgnoreEntityID;
    IssueOrder(EM, EntityID, MoveOrder, false);

    if (auto* Path = EM.GetComponent<PathfindingComponent>(EntityID))
        return !Path->Path.empty();

    return false;
}

void CommandSystem::IssueAttackMove(Core::ECS::EntityManager& EM,
                                     const std::vector<int>& Units,
                                     Core::Math::Vec2 Target)
{
    Order AtkOrder;
    AtkOrder.Type = OrderType::AttackMove;
    AtkOrder.TargetPosition = Target;
    IssueOrder(EM, Units, AtkOrder, false);
}

void CommandSystem::QueueOrder(Core::ECS::EntityManager& EM,
                                Core::ECS::EntityID EntityID,
                                const Order& InOrder)
{
    auto* Cmd = EM.GetComponent<CommandComponent>(EntityID);
    if (!Cmd)
        Cmd = &EM.AddComponent<CommandComponent>(EntityID);
    Cmd->Orders.push_back(InOrder);
}

void CommandSystem::CancelOrder(Core::ECS::EntityManager& EM,
                                 Core::ECS::EntityID EntityID,
                                 int OrderIndex)
{
    auto* Cmd = EM.GetComponent<CommandComponent>(EntityID);
    if (!Cmd) return;
    Cmd->CancelOrder(OrderIndex);
}

void CommandSystem::CancelAll(Core::ECS::EntityManager& EM,
                               Core::ECS::EntityID EntityID)
{
    auto* Cmd = EM.GetComponent<CommandComponent>(EntityID);
    if (!Cmd) return;
    Cmd->CancelAll();
}

void CommandSystem::SetRallyPoint(Core::ECS::EntityManager& EM,
                                   Core::ECS::EntityID EntityID,
                                   const Core::Math::Vec2& Point)
{
    auto* Cmd = EM.GetComponent<CommandComponent>(EntityID);
    if (!Cmd)
        Cmd = &EM.AddComponent<CommandComponent>(EntityID);
    Cmd->RallyPoint = Point;
    Cmd->HasRallyPoint = true;
}

bool CommandSystem::CanSetSiegeMode(Core::ECS::EntityManager& EM,
                                    Core::ECS::EntityID EntityID,
                                    bool Siege)
{
    const auto* Unit = EM.GetComponent<UnitComponent>(EntityID);
    const auto* Player = EM.GetComponent<PlayerComponent>(EntityID);
    if (!Unit || !Player || Unit->HitPoints <= 0)
        return false;
    if (const auto* Transition = EM.GetComponent<SiegeTransitionComponent>(EntityID);
        Transition && Transition->Active)
    {
        return false;
    }

    if (Siege)
    {
        return Unit->ChkUnitID == 5 &&
               UpgradeSystem::HasResearch(Player->PlayerID, ResearchId::Terran_SiegeMode);
    }
    return Unit->ChkUnitID == 30;
}

bool CommandSystem::SetSiegeMode(Core::ECS::EntityManager& EM,
                                 Core::ECS::EntityID EntityID,
                                 bool Siege)
{
    if (!CanSetSiegeMode(EM, EntityID, Siege))
        return false;

    auto* Unit = EM.GetComponent<UnitComponent>(EntityID);
    if (!Unit)
        return false;

    const auto TankDef = Game::Races::GetUnitDefByName("SiegeTank");
    const int TargetChkUnitID = Siege ? 30 : TankDef.ChkUnitID;
    auto* Transition = EM.GetComponent<SiegeTransitionComponent>(EntityID);
    if (!Transition)
        Transition = &EM.AddComponent<SiegeTransitionComponent>(EntityID);
    *Transition = SiegeTransitionComponent{};
    Transition->Active = true;
    Transition->TargetSieged = Siege;
    Transition->TargetChkUnitID = TargetChkUnitID;
    Transition->TargetSpeed = Siege ? 0.0f : TankDef.Speed;
    Transition->TargetImageID = sCommandImageResolver
        ? sCommandImageResolver(TargetChkUnitID)
        : static_cast<uint32_t>(TargetChkUnitID);
    Transition->TargetSubunitImageID = sCommandSubunitImageResolver
        ? sCommandSubunitImageResolver(TargetChkUnitID)
        : std::numeric_limits<uint32_t>::max();
    Unit->Speed = 0.0f;

    if (auto* Combat = EM.GetComponent<CombatComponent>(EntityID))
    {
        Transition->TargetCombat = *Combat;
        Game::Races::ApplyCombatDefinition(Transition->TargetCombat, TankDef, false);
        Transition->TargetCombat.MinimumAttackRange = 0.0f;
        if (Siege)
        {
            Transition->TargetCombat.GroundDamage = 70;
            Transition->TargetCombat.GroundDamageBonus = 5;
            Transition->TargetCombat.GroundHitCount = 1;
            Transition->TargetCombat.GroundDmgType = DamageType::Explosive;
            Transition->TargetCombat.AttackRange = 12.0f;
            Transition->TargetCombat.GroundAttackRange = 12.0f;
            Transition->TargetCombat.MinimumAttackRange = 2.0f;
            Transition->TargetCombat.AttackCooldown = 75.0f / 24.0f;
            Transition->TargetCombat.GroundAttackCooldown = 75.0f / 24.0f;
            Transition->TargetCombat.Splash = SplashType::Radial;
            Transition->TargetCombat.SplashInnerRadius = 10.0f / 32.0f;
            Transition->TargetCombat.SplashMediumRadius = 25.0f / 32.0f;
            Transition->TargetCombat.SplashRadius = 40.0f / 32.0f;
            Transition->TargetCombat.IsRanged = true;
            Transition->TargetCombat.CanTargetGround = true;
        }
        Combat->PendingAttackFrame = false;
        Combat->PendingAttackTargetEntityID = -1;
        Combat->CanTargetGround = false;
        Combat->CanTargetAir = false;
    }

    if (auto* Path = EM.GetComponent<PathfindingComponent>(EntityID))
    {
        Path->Path.clear();
        Path->PathIndex = 0;
        Path->CurrentSpeed = 0.0f;
        Path->TargetSpeed = 0.0f;
        Path->CurrentVelocity = {};
    }
    if (auto* Command = EM.GetComponent<CommandComponent>(EntityID))
    {
        Command->CancelAll();
        Command->CommandQueue.clear();
        Command->CurrentCommand = CommandType::Stop;
        Command->TargetEntityID = -1;
    }
    return true;
}

void CommandSystem::AdvanceQueue(Core::ECS::EntityManager& EM,
                                  Core::ECS::EntityID EntityID)
{
    auto* Cmd = EM.GetComponent<CommandComponent>(EntityID);
    if (!Cmd) return;
    Cmd->AdvanceQueue();
    if (const Order* CurrentOrder = Cmd->GetCurrentOrder())
    {
        ConfigureMovementPathForOrder(EM, EntityID, *CurrentOrder);
    }
    else
    {
        Cmd->CurrentCommand = CommandType::Stop;
        Cmd->TargetEntityID = -1;
    }
}

} // namespace Game::Systems
