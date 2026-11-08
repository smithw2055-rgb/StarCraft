#include "Game/Systems/CommandSystem.h"
#include "Game/Components.h"
#include "Game/Pathfinding/AStar.h"
#include "Game/Pathfinding/NavigationService.h"
#include "Game/Pathfinding/NavigationSpatialHash.h"
#include "Game/Pathfinding/NavigationWorldResource.h"
#include "Game/Pathfinding/WorldSpatialIndexSystem.h"
#include "Game/SimulationContext.h"
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
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <vector>

namespace Game::Systems
{

using namespace Game::Components;

const Game::Map::GameMap* gCmdMap = nullptr;
static Game::Pathfinding::NavigationService sNavigationService;
static CommandSystem::ImageIDResolver sCommandImageResolver;
static CommandSystem::ImageIDResolver sCommandSubunitImageResolver;

static bool PathingDebugEnabled()
{
    const char* Value = std::getenv("STARCRAFT_DEBUG_PATHING");
    return Value && Value[0] != '\0' && Value[0] != '0';
}

static const Game::Map::GameMap* ResolveCmdMap(
    const Core::ECS::EntityManager& EM,
    const Game::Map::GameMap* ExplicitMap = nullptr)
{
    if (ExplicitMap)
        return ExplicitMap;
    return Game::Pathfinding::NavigationWorldResource::ResolveMap(EM, gCmdMap);
}

static Game::Pathfinding::NavigationService& ResolveNavService(
    Core::ECS::EntityManager& EM,
    const Game::Map::GameMap* ExplicitMap = nullptr)
{
    const Game::Map::GameMap* EffectiveMap = ResolveCmdMap(EM, ExplicitMap);
    auto& NavRes = Game::Pathfinding::NavigationWorldResource::GetOrSync(EM, EffectiveMap);
    return NavRes.NavService;
}

void CommandSystem::SetMap(const Game::Map::GameMap* Map)
{
    gCmdMap = Map;
    if (Map)
        sNavigationService.SetBaseGrid(Map->GetNavigationGrid());
    if (auto* ActiveEM = Core::ECS::EntityManager::GetActiveWorld())
    {
        Game::Pathfinding::NavigationWorldResource::GetOrSync(*ActiveEM, Map);
    }
    MovementSystem::SetPathRequestCallback(&CommandSystem::RequestMovementPath);
    MovementSystem::SetNavigationSyncCallback(
        &CommandSystem::SyncNavigationState);
}


void CommandSystem::SetImageResolver(ImageIDResolver Resolver)
{
    sCommandImageResolver = std::move(Resolver);
}

void CommandSystem::SetSubunitImageResolver(ImageIDResolver Resolver)
{
    sCommandSubunitImageResolver = std::move(Resolver);
}

const Game::Pathfinding::NavigationPerformanceStats&
CommandSystem::GetNavigationStats()
{
    if (auto* ActiveEM = Core::ECS::EntityManager::GetActiveWorld())
    {
        if (const auto* NavRes = ActiveEM->GetResource<Game::Pathfinding::NavigationWorldResource>())
            return NavRes->NavService.GetStats();
    }
    return sNavigationService.GetStats();
}

void CommandSystem::ResetNavigationStats()
{
    if (auto* ActiveEM = Core::ECS::EntityManager::GetActiveWorld())
    {
        if (auto* NavRes = ActiveEM->GetResource<Game::Pathfinding::NavigationWorldResource>())
            NavRes->NavService.ResetStats();
    }
    sNavigationService.ResetStats();
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
    if (ResearchID == ResearchId::Terran_CloakingField ||
        ResearchID == ResearchId::Terran_PersonalCloaking)
    {
        auto* Player = EM.GetComponent<PlayerComponent>(EntityID);
        const int PlayerID = Player ? Player->PlayerID : 0;
        if (!UpgradeSystem::HasResearch(PlayerID, ResearchID))
            return false;

        auto* Unit = EM.GetComponent<UnitComponent>(EntityID);
        if (!Unit || Unit->HitPoints <= 0)
            return false;

        auto* Cloak = EM.GetComponent<CloakComponent>(EntityID);
        if (!Cloak)
            Cloak = &EM.AddComponent<CloakComponent>(EntityID);

        if (Cloak->IsCloaked)
        {
            Cloak->IsCloaked = false;
            Cloak->CloakEnergyCost = 0.0f;
            return true;
        }
        else
        {
            if (Unit->EnergyPoints < 25)
                return false;
            Unit->EnergyPoints -= 25;
            Cloak->IsCloaked = true;
            Cloak->IsPassiveCloak = false;
            Cloak->CloakEnergyCost = 1.25f;
            Unit->CloakEnergyAccumulator = 0.0f;
            return true;
        }
    }

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
        Path->StallTicks = 0;
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
            case 131: // Zerg Hatchery
            case 132: // Zerg Lair
            case 133: // Zerg Hive
            case 154: // Protoss Nexus
                Half = {64.0f, 48.0f};
                break;
            case 111: // Terran Barracks
            case 113: // Terran Factory
            case 114: // Terran Starport
            case 116: // Terran Science Facility
            case 160: // Protoss Gateway
            case 167: // Protoss Stargate
                Half = {56.0f, 40.0f};
                break;
            case 107: // Terran Comsat Station
            case 108: // Terran Nuclear Silo
            case 109: // Terran Supply Depot
            case 115: // Terran Control Tower
            case 120: // Terran Machine Shop
            case 124: // Terran Missile Turret
            case 125: // Terran Bunker
            case 156: // Protoss Pylon
            case 162: // Protoss Photon Cannon
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

static std::vector<Game::Pathfinding::NavigationObstacle>
BuildNavigationObstacles(Core::ECS::EntityManager& EM,
                         Core::ECS::EntityID MovingID,
                         Core::ECS::EntityID AdditionalDynamicBlockerID = -1)
{
    std::vector<Game::Pathfinding::NavigationObstacle> Obstacles;
    const auto AddObstacle = [&](int EntityID) {
        if (EntityID == MovingID)
            return;
        if (!IsStaticGroundBlocker(EM, EntityID) &&
            EntityID != AdditionalDynamicBlockerID)
        {
            return;
        }

        const StaticBlockerRect Rect =
            StaticBlockerRectForEntity(EM, EntityID);
        Game::Pathfinding::NavigationObstacle Obstacle;
        Obstacle.OwnerEntityID = EntityID;
        Obstacle.Position = Rect.Position;
        Obstacle.Left = std::max(0, (int)std::ceil(Rect.Left));
        Obstacle.Up = std::max(0, (int)std::ceil(Rect.Top));
        Obstacle.Right = std::max(0, (int)std::ceil(Rect.Right));
        Obstacle.Down = std::max(0, (int)std::ceil(Rect.Bottom));
        Obstacles.push_back(Obstacle);
    };

    for (const auto EntityID : EM.GetEntitiesWithCached<BuildingComponent, TransformComponent>())
    {
        AddObstacle(EntityID);
    }
    for (const auto EntityID : EM.GetEntitiesWithCached<ResourceComponent, TransformComponent>())
    {
        AddObstacle(EntityID);
    }
    if (AdditionalDynamicBlockerID >= 0 &&
        !EM.GetComponent<BuildingComponent>(AdditionalDynamicBlockerID) &&
        !EM.GetComponent<ResourceComponent>(AdditionalDynamicBlockerID))
    {
        AddObstacle(AdditionalDynamicBlockerID);
    }
    return Obstacles;
}

void CommandSystem::SyncNavigationState(
    Core::ECS::EntityManager& EM,
    const Game::Map::GameMap* Map)
{
    const Game::Map::GameMap* EffectiveMap = ResolveCmdMap(EM, Map);
    if (!EffectiveMap)
        return;
    auto& NavSvc = ResolveNavService(EM, EffectiveMap);
    NavSvc.SetBaseGrid(EffectiveMap->GetNavigationGrid());
    NavSvc.SyncStaticObstacles(
        BuildNavigationObstacles(EM, -1));
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

    auto& SharedSpatial = Game::Pathfinding::SpatialIndexResource::AcquireUpToDate(EM);
    static thread_local std::vector<int> sSegmentBlockers;
    Core::Math::Vec2 SegmentStart = Start;
    for (const auto& Waypoint : Waypoints)
    {
        const float SegMinX = std::min(SegmentStart.X, Waypoint.X) - 128.0f;
        const float SegMaxX = std::max(SegmentStart.X, Waypoint.X) + 128.0f;
        const float SegMinY = std::min(SegmentStart.Y, Waypoint.Y) - 128.0f;
        const float SegMaxY = std::max(SegmentStart.Y, Waypoint.Y) + 128.0f;

        SharedSpatial.AllUnitsHash.QueryInto(SegMinX, SegMinY, SegMaxX, SegMaxY, sSegmentBlockers);
        std::sort(sSegmentBlockers.begin(), sSegmentBlockers.end());
        for (int BlockerID : sSegmentBlockers)
        {
            if (BlockerID == MovingID || BlockerID == IgnoreEntityID)
                continue;

            auto* Trans = EM.GetComponent<TransformComponent>(BlockerID);
            if (!Trans || Trans->Position.X < SegMinX || Trans->Position.X > SegMaxX ||
                Trans->Position.Y < SegMinY || Trans->Position.Y > SegMaxY)
            {
                continue;
            }

            if (!IsStaticGroundBlocker(EM, BlockerID))
                continue;

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
        Path->DestinationStallTicks = 0;
        Path->HasLastDestinationDistance = false;
        if (!Path->HasRecentMoveFailure ||
            Path->RecentFailedMoveTarget.DistanceTo(Target) > 16.0f)
        {
            Path->HasRecentMoveFailure = false;
            Path->RecentMoveFailureCooldown = 0.0f;
            Path->RecentMoveFailureTicks = 0;
        }
    }

    if (Path->IsAir || !Map)
    {
        Path->Path.push_back(Target);
        Path->LastPathStatus = Game::Pathfinding::PathStatus::Exact;
        Path->LastPathEndReason = Game::Pathfinding::PathEndReason::None;
        Path->ResolvedPathDestination = Target;
        Path->NavigationVersion =
            Map ? Map->GetNavigationGrid().GetVersion() : 0;
        Path->LastPathExpandedCells = 0;
        return true;
    }

    auto& NavSvc = ResolveNavService(EM, Map);
    NavSvc.SetBaseGrid(Map->GetNavigationGrid());
    auto Obstacles = BuildNavigationObstacles(EM, EntityID, -1);

    Game::Pathfinding::NavigationPathQuery Query;
    Query.Start = Trans->Position;
    Query.Target = Target;
    Query.IgnoredOwnerEntityID = PathingIgnoreEntityID;
    Path->RequestedAdditionalIgnoreEntityID = -1;
    if (const auto* Harvest = EM.GetComponent<HarvestComponent>(EntityID);
        Harvest &&
        Harvest->State == HarvestState::MovingToResource &&
        PathingIgnoreEntityID >= 0 &&
        PathingIgnoreEntityID != Harvest->TargetResourceID)
    {
        const auto* TargetRes =
            EM.GetComponent<ResourceComponent>(Harvest->TargetResourceID);
        Query.IgnoredOwnerEntityID =
            (TargetRes && TargetRes->Type == ResourceType::VespeneGas)
                ? Harvest->TargetResourceID
                : -1;
        Query.AdditionalIgnoredOwnerEntityID = PathingIgnoreEntityID;
    }
    for (const auto& Obs : Obstacles)
    {
        if (Obs.OwnerEntityID == Query.IgnoredOwnerEntityID)
            continue;
        if (std::fabs(Trans->Position.X - Obs.Position.X) >
                (float)Obs.Left + (float)Obs.Right + 2.0f ||
            std::fabs(Trans->Position.Y - Obs.Position.Y) >
                (float)Obs.Up + (float)Obs.Down + 2.0f)
        {
            continue;
        }
        // Only drop a static blocker from the plan when the unit is genuinely
        // inside its real (unexpanded) footprint and therefore cannot take a
        // legal first step. Using the mover-expanded rectangle here treated a
        // unit that merely touched a building as "embedded": every repath
        // ignored the building, produced a straight route through it, and left
        // the unit pinned against the edge oscillating in place.
        const auto BlockerRect = StaticBlockerRectForEntity(
            EM, Obs.OwnerEntityID);
        if (PointInsideStaticBlockerRect(Trans->Position, BlockerRect))
        {
            Query.AdditionalIgnoredOwnerEntityID = Obs.OwnerEntityID;
            Path->RequestedAdditionalIgnoreEntityID = Obs.OwnerEntityID;
            break;
        }
    }

    NavSvc.SyncStaticObstacles(std::move(Obstacles));
    if (AvoidDynamicEntityID >= 0 &&
        AvoidDynamicEntityID != EntityID &&
        !IsStaticGroundBlocker(EM, AvoidDynamicEntityID))
    {
        const StaticBlockerRect DynamicRect =
            StaticBlockerRectForEntity(EM, AvoidDynamicEntityID);
        if (PointInsideStaticBlockerRect(Trans->Position, DynamicRect) &&
            Query.AdditionalIgnoredOwnerEntityID < 0)
        {
            Query.AdditionalIgnoredOwnerEntityID = AvoidDynamicEntityID;
            Path->RequestedAdditionalIgnoreEntityID = AvoidDynamicEntityID;
        }
        Query.HasAvoidedObstacle = true;
        Query.AvoidedObstacle.OwnerEntityID = AvoidDynamicEntityID;
        Query.AvoidedObstacle.Position = DynamicRect.Position;
        Query.AvoidedObstacle.Left = std::max(0, (int)std::ceil(DynamicRect.Left));
        Query.AvoidedObstacle.Up = std::max(0, (int)std::ceil(DynamicRect.Top));
        Query.AvoidedObstacle.Right = std::max(0, (int)std::ceil(DynamicRect.Right));
        Query.AvoidedObstacle.Down = std::max(0, (int)std::ceil(DynamicRect.Bottom));
    }

    Query.MaximumExpandedCells = 16384;
    if (Path->HasCollisionBounds)
    {
        Query.Footprint.Left = Path->CollisionLeft;
        Query.Footprint.Up = Path->CollisionUp;
        Query.Footprint.Right = Path->CollisionRight;
        Query.Footprint.Down = Path->CollisionDown;
    }
    else
    {
        Query.Footprint.Left = Path->CollisionRadius;
        Query.Footprint.Up = Path->CollisionRadius;
        Query.Footprint.Right = Path->CollisionRadius;
        Query.Footprint.Down = Path->CollisionRadius;
    }

    auto Result = NavSvc.QueryPath(Query);
    Path->RequestedAdditionalIgnoreEntityID =
        Result.AdditionalIgnoredOwnerEntityID;
    Path->LastPathStatus = Result.Status;
    Path->LastPathEndReason = Result.EndReason;
    Path->ResolvedPathDestination = Result.ResolvedDestination;
    Path->NavigationVersion = Result.NavigationVersion;
    Path->LastPathExpandedCells = Result.ExpandedCells;
    bool HasUsablePath = Result.HasUsablePath();
    if (HasUsablePath &&
        Result.Status == Game::Pathfinding::PathStatus::Partial &&
        Query.IgnoredOwnerEntityID >= 0)
    {
        const int TargetIgnoreID = Query.IgnoredOwnerEntityID;
        const auto* InteractionTransform =
            EM.GetComponent<TransformComponent>(TargetIgnoreID);
        const auto InteractionRect = ExpandedStaticBlockerRectForMover(
            EM, TargetIgnoreID, *Path);
        const float MinimumX =
            InteractionRect.Position.X - InteractionRect.Left;
        const float MaximumX =
            InteractionRect.Position.X + InteractionRect.Right;
        const float MinimumY =
            InteractionRect.Position.Y - InteractionRect.Top;
        const float MaximumY =
            InteractionRect.Position.Y + InteractionRect.Bottom;
        const float GapX = std::max(
            0.0f,
            std::max(MinimumX - Result.ResolvedDestination.X,
                     Result.ResolvedDestination.X - MaximumX));
        const float GapY = std::max(
            0.0f,
            std::max(MinimumY - Result.ResolvedDestination.Y,
                     Result.ResolvedDestination.Y - MaximumY));
        float InteractionTolerance = 2.0f;
        if (const auto* Res = EM.GetComponent<ResourceComponent>(TargetIgnoreID);
            Res && Res->Type == ResourceType::VespeneGas)
        {
            InteractionTolerance = 48.0f;
        }
        else if (EM.GetComponent<BuildingComponent>(TargetIgnoreID))
        {
            InteractionTolerance = 32.0f;
        }
        HasUsablePath = InteractionTransform &&
            GapX * GapX + GapY * GapY <=
                InteractionTolerance * InteractionTolerance;
        if (!HasUsablePath)
        {
            const std::vector<Core::Math::Vec2> DirectSegment{Target};
            Path->BlockedByEntityID = FindExactStaticBlockerOnPath(
                EM, EntityID, Trans->Position, DirectSegment, *Path,
                PathingIgnoreEntityID);
            Path->HasRecentMoveFailure = true;
            Path->RecentFailedMoveTarget = Target;
            Path->RecentMoveFailureTicks = 24;
            Path->RecentMoveFailureCooldown = 1.0f;
            Path->CurrentSpeed = 0.0f;
            Path->TargetSpeed = 0.0f;
            Path->CurrentVelocity = {};
        }
    }
    if (!HasUsablePath && !Path->HasRecentMoveFailure)
    {
        const std::vector<Core::Math::Vec2> DirectSegment{Target};
        Path->BlockedByEntityID = FindExactStaticBlockerOnPath(
            EM, EntityID, Trans->Position, DirectSegment, *Path,
            PathingIgnoreEntityID);
        Path->HasRecentMoveFailure = true;
        Path->RecentFailedMoveTarget = Target;
        Path->RecentMoveFailureTicks = 24;
        Path->RecentMoveFailureCooldown = 1.0f;
        Path->CurrentSpeed = 0.0f;
        Path->TargetSpeed = 0.0f;
        Path->CurrentVelocity = {};
    }
    if (HasUsablePath)
    {
        Path->Path = std::move(Result.Waypoints);
        if (DestinationChanged ||
            Result.Status == Game::Pathfinding::PathStatus::Exact ||
            Trans->Position.DistanceTo(Result.ResolvedDestination) > 24.0f)
        {
            Path->ConsecutiveMoveFailures = 0;
        }
        Path->RecentMoveFailureNavigationVersion = Result.NavigationVersion;
    }
    else
    {
        const bool SameFailedRequest =
            !DestinationChanged &&
            Path->RecentMoveFailureNavigationVersion == Result.NavigationVersion;
        Path->ConsecutiveMoveFailures = SameFailedRequest
            ? std::min(Path->ConsecutiveMoveFailures + 1, 8)
            : 1;
        Path->RecentMoveFailureNavigationVersion = Result.NavigationVersion;
        const bool HarvestOwnsRetry =
            EM.GetComponent<HarvestComponent>(EntityID) != nullptr;
        const int BackoffTicks = HarvestOwnsRetry
            ? 24
            : std::min(
                240, 24 << std::min(Path->ConsecutiveMoveFailures - 1, 3));
        Path->RecentMoveFailureTicks = BackoffTicks;
        Path->RecentMoveFailureCooldown = (float)BackoffTicks / 24.0f;
    }

    if (PathingDebugEnabled())
    {
        const auto* Unit = EM.GetComponent<UnitComponent>(EntityID);
        const auto* Command = EM.GetComponent<CommandComponent>(EntityID);
        const auto* Harvest = EM.GetComponent<HarvestComponent>(EntityID);
        const auto StatusName =
            Result.Status == Game::Pathfinding::PathStatus::Exact ? "Exact" :
            Result.Status == Game::Pathfinding::PathStatus::Partial ? "Partial" :
            "Unreachable";
        const auto EndReasonName =
            Result.EndReason == Game::Pathfinding::PathEndReason::None ? "None" :
            Result.EndReason == Game::Pathfinding::PathEndReason::GoalBlocked ?
                "GoalBlocked" :
            Result.EndReason == Game::Pathfinding::PathEndReason::NodeBudget ?
                "NodeBudget" : "Disconnected";
        Core::Utils::Logger::Info("Pathing",
            "request entity=" + std::to_string(EntityID) +
            " unit=" + std::to_string(Unit ? Unit->ChkUnitID : -1) +
            " start=" + std::to_string((int)Trans->Position.X) + "," +
                std::to_string((int)Trans->Position.Y) +
            " target=" + std::to_string((int)Target.X) + "," +
                std::to_string((int)Target.Y) +
            " resolved=" +
                std::to_string((int)Result.ResolvedDestination.X) + "," +
                std::to_string((int)Result.ResolvedDestination.Y) +
            " status=" + StatusName +
            " reason=" + EndReasonName +
            " nav_version=" + std::to_string(Result.NavigationVersion) +
            " expanded=" + std::to_string(Result.ExpandedCells) +
            " waypoints=" + std::to_string(Path->Path.size()) +
            " command=" +
                std::to_string(Command ? (int)Command->CurrentCommand : -1) +
            " harvest_state=" +
                std::to_string(Harvest ? (int)Harvest->State : -1) +
            " ignore_entity=" + std::to_string(PathingIgnoreEntityID) +
            " avoid_entity=" + std::to_string(AvoidDynamicEntityID));
    }
    return HasUsablePath && !Path->Path.empty();
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
    Path->StallTicks = 0;
    Path->HasLastWaypointDistance = false;
    Path->DestinationStallTimer = 0.0f;
    Path->DestinationStallTicks = 0;
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
        Path->RecentMoveFailureTicks = 0;
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
        ResolveCmdMap(EM),
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

bool CommandSystem::IsTerranRepairableUnit(const UnitComponent& Unit)
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

bool CommandSystem::IsRepairableTarget(Core::ECS::EntityManager& EM, int TargetEntityID)
{
    auto* TargetUnit = EM.GetComponent<UnitComponent>(TargetEntityID);
    if (!TargetUnit || TargetUnit->UnitRace != Race::Terran || TargetUnit->HitPoints <= 0)
    {
        return false;
    }
    if (TargetUnit->HitPoints >= TargetUnit->MaxHitPoints &&
        TargetUnit->HitPointSubDamage == 0)
    {
        return false;
    }
    auto* TargetBuilding = EM.GetComponent<BuildingComponent>(TargetEntityID);
    if (TargetBuilding)
    {
        return !TargetBuilding->UnderConstruction;
    }
    return IsTerranRepairableUnit(*TargetUnit);
}

void CommandSystem::Update(float DeltaTime)
{
    if (!mSystemMgr) return;
    const Game::Map::GameMap* ActiveMap = ResolveCmdMap(mSystemMgr->GetEntityManager());
    if (DeltaTime <= 0.0f || !ActiveMap) return;

    const char* ProfileSystems = std::getenv("STARCRAFT_PROFILE_SYSTEMS");
    if (ProfileSystems && ProfileSystems[0] != '\0' &&
        ProfileSystems[0] != '0')
    {
        static auto LastNavigationReport =
            std::chrono::steady_clock::now();
        static Game::Pathfinding::NavigationPerformanceStats LastStats;
        const auto Now = std::chrono::steady_clock::now();
        if (Now - LastNavigationReport >= std::chrono::seconds(1))
        {
            const auto& Current = ResolveNavService(mSystemMgr->GetEntityManager(), ActiveMap).GetStats();
            const auto Delta = [](uint64_t CurrentValue,
                                  uint64_t PreviousValue)
            {
                return CurrentValue >= PreviousValue
                    ? CurrentValue - PreviousValue
                    : CurrentValue;
            };
            const uint64_t Queries =
                Delta(Current.QueryCount, LastStats.QueryCount);
            const uint64_t QueryMicroseconds =
                Delta(Current.QueryMicroseconds,
                      LastStats.QueryMicroseconds);
            const uint64_t Expanded =
                Delta(Current.ExpandedCells, LastStats.ExpandedCells);
            Core::Utils::Logger::Info(
                "Profile",
                "navigation queries=" + std::to_string(Queries) +
                " query_us=" + std::to_string(QueryMicroseconds) +
                " average_us=" +
                    std::to_string(Queries > 0
                        ? QueryMicroseconds / Queries : 0) +
                " expanded=" + std::to_string(Expanded) +
                " cache_hits=" +
                    std::to_string(Delta(
                        Current.CorridorCacheHits,
                        LastStats.CorridorCacheHits)) +
                " partial=" +
                    std::to_string(Delta(
                        Current.PartialPaths, LastStats.PartialPaths)) +
                " failed=" +
                    std::to_string(Delta(
                        Current.FailedPaths, LastStats.FailedPaths)));
            LastStats = Current;
            LastNavigationReport = Now;
        }
    }

    auto& EM = mSystemMgr->GetEntityManager();
    auto Units = EM.GetEntitiesWith<CommandComponent, TransformComponent>();

    auto& SharedSpatial = Game::Pathfinding::SpatialIndexResource::AcquireUpToDate(EM);
    const auto& sCommandSpatialHash = SharedSpatial.AllUnitsHash;
    static thread_local std::vector<std::pair<int, int>> sActiveMedicHealTargets;
    sActiveMedicHealTargets.clear();
    for (int Eid : EM.GetEntitiesWithCached<TransformComponent, UnitComponent>())
    {
        if (const auto* U = EM.GetComponent<UnitComponent>(Eid);
            U && U->HitPoints > 0 && U->HealTargetEntityID >= 0)
        {
            sActiveMedicHealTargets.emplace_back(Eid, U->HealTargetEntityID);
        }
    }

    auto IsTargetHealedByOtherMedic = [&](int MyMedicID, int TargetID) -> bool {
        for (const auto& [OtherMedicID, HealTargetID] : sActiveMedicHealTargets)
        {
            if (OtherMedicID == MyMedicID || HealTargetID != TargetID)
                continue;
            const auto* OtherMedic = EM.GetComponent<UnitComponent>(OtherMedicID);
            if (OtherMedic && OtherMedic->HitPoints > 0 &&
                OtherMedic->HealTargetEntityID == TargetID)
            {
                return true;
            }
        }
        return false;
    };

    auto UpdateActiveMedicHealTarget = [&](int MedicID, int NewTargetID) {
        for (auto It = sActiveMedicHealTargets.begin(); It != sActiveMedicHealTargets.end(); ++It)
        {
            if (It->first == MedicID)
            {
                if (NewTargetID >= 0)
                    It->second = NewTargetID;
                else
                    sActiveMedicHealTargets.erase(It);
                return;
            }
        }
        if (NewTargetID >= 0)
            sActiveMedicHealTargets.emplace_back(MedicID, NewTargetID);
    };

    for (int ID : Units)
    {
        auto* Cmd = EM.GetComponent<CommandComponent>(ID);
        if (!Cmd) continue;

        if (Cmd->RallyDisplayTimer > 0.0f)
        {
            Cmd->RallyDisplayTimer = std::max(0.0f, Cmd->RallyDisplayTimer - DeltaTime);
        }

        if (!Cmd->HasPendingOrders()) continue;

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

                static thread_local std::vector<int> GuardNeighbors;
                GuardNeighbors.clear();
                sCommandSpatialHash.QueryInto(
                    MyTrans->Position.X - BestDist, MyTrans->Position.Y - BestDist,
                    MyTrans->Position.X + BestDist, MyTrans->Position.Y + BestDist,
                    GuardNeighbors);
                for (int Eid : GuardNeighbors)
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

                if (auto* MyUnit = EM.GetComponent<UnitComponent>(ID);
                    MyUnit && MyUnit->ChkUnitID == 34 && MyUnit->EnergyPoints > 0)
                {
                    auto* MyPlayer = EM.GetComponent<PlayerComponent>(ID);
                    auto* MyTrans = EM.GetComponent<TransformComponent>(ID);
                    if (MyPlayer && MyTrans)
                    {
                        int BestTarget = -1;
                        float BestDist = 160.0f;
                        static thread_local std::vector<int> MedicStopNeighbors;
                        MedicStopNeighbors.clear();
                        sCommandSpatialHash.QueryInto(
                            MyTrans->Position.X - BestDist, MyTrans->Position.Y - BestDist,
                            MyTrans->Position.X + BestDist, MyTrans->Position.Y + BestDist,
                            MedicStopNeighbors);
                        for (int Eid : MedicStopNeighbors)
                        {
                            if (Eid == ID) continue;
                            auto* EUnit = EM.GetComponent<UnitComponent>(Eid);
                            auto* EPlayer = EM.GetComponent<PlayerComponent>(Eid);
                            auto* ETrans = EM.GetComponent<TransformComponent>(Eid);
                            if (!EUnit || !EPlayer || !ETrans) continue;
                            if (EPlayer->PlayerID != MyPlayer->PlayerID) continue;
                            if (EUnit->HitPoints <= 0 || EUnit->HitPoints >= EUnit->MaxHitPoints) continue;
                            if (EM.GetComponent<BuildingComponent>(Eid) != nullptr) continue;
                            if (ShouldMoveAsAir(EM, Eid)) continue;
                            if (!Game::Races::IsOrganicChkUnitID(EUnit->ChkUnitID)) continue;
                            if (IsTargetHealedByOtherMedic(ID, Eid)) continue;

                            float D = MyTrans->Position.DistanceTo(ETrans->Position);
                            if (D < BestDist)
                            {
                                BestDist = D;
                                BestTarget = Eid;
                            }
                        }
                        if (BestTarget >= 0)
                        {
                            MyUnit->HealTargetEntityID = BestTarget;
                            UpdateActiveMedicHealTarget(ID, BestTarget);
                            Components::Order HealOrder;
                            HealOrder.Type = OrderType::Heal;
                            HealOrder.TargetEntityID = BestTarget;
                            HealOrder.TargetPosition = EM.GetComponent<TransformComponent>(BestTarget)->Position;
                            IssueOrder(EM, ID, HealOrder, false);
                        }
                    }
                }
                break;
            }

            case OrderType::Follow:
            {
                // In addition to following its escort target, a Medic should immediately react and heal nearby wounded organics
                if (auto* MyUnit = EM.GetComponent<UnitComponent>(ID);
                    MyUnit && MyUnit->ChkUnitID == 34 && MyUnit->EnergyPoints > 0)
                {
                    auto* MyPlayer = EM.GetComponent<PlayerComponent>(ID);
                    auto* MyTrans = EM.GetComponent<TransformComponent>(ID);
                    if (MyPlayer && MyTrans)
                    {
                        int BestTarget = -1;
                        float BestDist = 160.0f;
                        static thread_local std::vector<int> MedicFollowNeighbors;
                        MedicFollowNeighbors.clear();
                        sCommandSpatialHash.QueryInto(
                            MyTrans->Position.X - BestDist, MyTrans->Position.Y - BestDist,
                            MyTrans->Position.X + BestDist, MyTrans->Position.Y + BestDist,
                            MedicFollowNeighbors);
                        for (int Eid : MedicFollowNeighbors)
                        {
                            if (Eid == ID) continue;
                            auto* EUnit = EM.GetComponent<UnitComponent>(Eid);
                            auto* EPlayer = EM.GetComponent<PlayerComponent>(Eid);
                            auto* ETrans = EM.GetComponent<TransformComponent>(Eid);
                            if (!EUnit || !EPlayer || !ETrans) continue;
                            if (EPlayer->PlayerID != MyPlayer->PlayerID) continue;
                            if (EUnit->HitPoints <= 0 || EUnit->HitPoints >= EUnit->MaxHitPoints) continue;
                            if (EM.GetComponent<BuildingComponent>(Eid) != nullptr) continue;
                            if (ShouldMoveAsAir(EM, Eid)) continue;
                            if (!Game::Races::IsOrganicChkUnitID(EUnit->ChkUnitID)) continue;
                            if (IsTargetHealedByOtherMedic(ID, Eid)) continue;

                            float D = MyTrans->Position.DistanceTo(ETrans->Position);
                            if (D < BestDist)
                            {
                                BestDist = D;
                                BestTarget = Eid;
                            }
                        }
                        if (BestTarget >= 0)
                        {
                            MyUnit->HealTargetEntityID = BestTarget;
                            UpdateActiveMedicHealTarget(ID, BestTarget);
                            Components::Order HealOrder;
                            HealOrder.Type = OrderType::Heal;
                            HealOrder.TargetEntityID = BestTarget;
                            HealOrder.TargetPosition = EM.GetComponent<TransformComponent>(BestTarget)->Position;
                            IssueOrder(EM, ID, HealOrder, false);
                            break;
                        }
                    }
                }

                // Escort target tracking for all following units
                if (Order->TargetEntityID >= 0)
                {
                    auto* TargetUnit = EM.GetComponent<UnitComponent>(Order->TargetEntityID);
                    auto* TargetTrans = EM.GetComponent<TransformComponent>(Order->TargetEntityID);
                    auto* MyTrans = EM.GetComponent<TransformComponent>(ID);
                    if (!TargetUnit || TargetUnit->HitPoints <= 0 || !TargetTrans || !MyTrans)
                    {
                        AdvanceQueue(EM, ID);
                        break;
                    }

                    const float Distance = MyTrans->Position.DistanceTo(TargetTrans->Position);
                    constexpr float FollowMinDist = 48.0f;
                    constexpr float FollowMaxDist = 72.0f;

                    if (Distance <= FollowMinDist)
                    {
                        if (auto* Path = EM.GetComponent<PathfindingComponent>(ID))
                        {
                            Path->Path.clear();
                            Path->PathIndex = 0;
                            Path->CurrentSpeed = 0.0f;
                            Path->TargetSpeed = 0.0f;
                            Path->CurrentVelocity = {0.0f, 0.0f};
                        }
                    }
                    else if (Distance > FollowMaxDist)
                    {
                        if (auto* Path = EM.GetComponent<PathfindingComponent>(ID))
                        {
                            const bool TargetMoved =
                                Path->RequestedDestination.DistanceTo(TargetTrans->Position) > 24.0f;
                            const bool PathEnded =
                                Path->Path.empty() || Path->PathIndex >= (int)Path->Path.size();

                            Cmd->TargetPosition = TargetTrans->Position;
                            Path->RequestedTargetEntityID = Order->TargetEntityID;

                            if (TargetMoved || (PathEnded && Path->LastRepathTime >= 0.25f))
                            {
                                RequestMovementPath(EM, ID, ActiveMap, TargetTrans->Position);
                                Path->RequestedTargetEntityID = Order->TargetEntityID;
                                Path->LastRepathTime = 0.0f;
                            }
                        }
                    }
                }
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
                          IsTerranRepairableUnit(*TargetUnit)));
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

            case OrderType::Heal:
            {
                bool ContinueHeal = false;
                if (Order->TargetEntityID >= 0)
                {
                    auto* TargetUnit = EM.GetComponent<UnitComponent>(Order->TargetEntityID);
                    auto* TargetPlayer = EM.GetComponent<PlayerComponent>(Order->TargetEntityID);
                    auto* MyPlayer = EM.GetComponent<PlayerComponent>(ID);
                    auto* MyUnit = EM.GetComponent<UnitComponent>(ID);
                    auto* MyTrans = EM.GetComponent<TransformComponent>(ID);
                    auto* TargetTrans = EM.GetComponent<TransformComponent>(Order->TargetEntityID);

                    if (TargetUnit && TargetPlayer && MyPlayer && MyUnit && MyTrans && TargetTrans &&
                        MyUnit->ChkUnitID == 34 && MyUnit->EnergyPoints > 0 &&
                        TargetPlayer->PlayerID == MyPlayer->PlayerID &&
                        TargetUnit->HitPoints > 0 && TargetUnit->HitPoints < TargetUnit->MaxHitPoints &&
                        EM.GetComponent<BuildingComponent>(Order->TargetEntityID) == nullptr &&
                        !ShouldMoveAsAir(EM, Order->TargetEntityID) &&
                        Game::Races::IsOrganicChkUnitID(TargetUnit->ChkUnitID))
                    {
                        if (!IsTargetHealedByOtherMedic(ID, Order->TargetEntityID))
                            ContinueHeal = true;
                    }
                    if (ContinueHeal)
                    {
                        if (MyUnit->HealTargetEntityID != Order->TargetEntityID)
                        {
                            MyUnit->HealTargetEntityID = Order->TargetEntityID;
                            MyUnit->HealHitPointAccumulator = 0.0f;
                            UpdateActiveMedicHealTarget(ID, Order->TargetEntityID);
                        }

                        const float Distance = MyTrans->Position.DistanceTo(TargetTrans->Position);
                        constexpr float HealRange = 64.0f;
                        if (Distance <= HealRange)
                        {
                            if (auto* Path = EM.GetComponent<PathfindingComponent>(ID))
                            {
                                Path->Path.clear();
                                Path->PathIndex = 0;
                                Path->CurrentSpeed = 0.0f;
                                Path->TargetSpeed = 0.0f;
                                Path->CurrentVelocity = {0.0f, 0.0f};
                            }

                            constexpr float HealRate = 15.0f;
                            MyUnit->HealHitPointAccumulator += HealRate * DeltaTime;
                            int HealPoints = static_cast<int>(MyUnit->HealHitPointAccumulator);
                            if (HealPoints > 0)
                            {
                                MyUnit->HealHitPointAccumulator -= static_cast<float>(HealPoints);
                                int MissingHP = TargetUnit->MaxHitPoints - TargetUnit->HitPoints;
                                int ActualHeal = std::min(MissingHP, HealPoints);
                                int EnergyNeeded = (ActualHeal + 1) / 2;
                                if (EnergyNeeded > MyUnit->EnergyPoints)
                                {
                                    EnergyNeeded = MyUnit->EnergyPoints;
                                    ActualHeal = EnergyNeeded * 2;
                                }

                                TargetUnit->HitPoints = std::min(TargetUnit->MaxHitPoints, TargetUnit->HitPoints + ActualHeal);
                                MyUnit->EnergyPoints = std::max(0, MyUnit->EnergyPoints - EnergyNeeded);

                                if (TargetUnit->HitPoints >= TargetUnit->MaxHitPoints || MyUnit->EnergyPoints <= 0)
                                {
                                    ContinueHeal = false;
                                }
                            }
                        }
                        else if (auto* Path = EM.GetComponent<PathfindingComponent>(ID))
                        {
                            const bool TargetMoved =
                                Path->RequestedDestination.DistanceTo(TargetTrans->Position) > 16.0f;
                            const bool PathEnded =
                                Path->Path.empty() || Path->PathIndex >= (int)Path->Path.size();

                            Cmd->TargetPosition = TargetTrans->Position;
                            Path->RequestedTargetEntityID = Order->TargetEntityID;

                            if (TargetMoved || (PathEnded && Path->LastRepathTime >= 0.25f))
                            {
                                RequestMovementPath(EM, ID, ActiveMap, TargetTrans->Position);
                                Path->RequestedTargetEntityID = Order->TargetEntityID;
                                Path->LastRepathTime = 0.0f;
                            }
                        }
                    }
                }

                if (!ContinueHeal)
                {
                    if (auto* Medic = EM.GetComponent<UnitComponent>(ID))
                    {
                        Medic->HealTargetEntityID = -1;
                        Medic->HealHitPointAccumulator = 0.0f;
                    }
                    UpdateActiveMedicHealTarget(ID, -1);
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
    std::vector<Core::ECS::EntityID> OrderedEntities = Entities;
    std::sort(OrderedEntities.begin(), OrderedEntities.end());
    OrderedEntities.erase(
        std::unique(OrderedEntities.begin(), OrderedEntities.end()),
        OrderedEntities.end());

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
        OrderedEntities.size() >= 2 &&
        InOrder.TargetEntityID < 0 &&
        IsGroupPositionOrder(InOrder.Type);

    if (AdjustGroupPositions)
    {
        for (Core::ECS::EntityID ID : OrderedEntities)
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
    for (Core::ECS::EntityID ID : OrderedEntities)
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

                if (gCmdMap && !Group.TargetInsideBounds)
                {
                    const int OriginHeight = gCmdMap->GetGroundHeightAtWorld(InOrder.TargetPosition);
                    const int TargetHeight = gCmdMap->GetGroundHeightAtWorld(EntityOrder.TargetPosition);
                    if (OriginHeight != TargetHeight)
                    {
                        EntityOrder.TargetPosition = InOrder.TargetPosition;
                    }
                    else
                    {
                        const float CollisionR = (float)CollisionRadiusForUnit(EM.GetComponent<UnitComponent>(ID));
                        const int R = (int)std::ceil(CollisionR);
                        const Game::Pathfinding::NavigationFootprint Footprint{R, R, R, R};
                        if (!gCmdMap->GetNavigationGrid().HasLineOfSightWorld(
                                InOrder.TargetPosition, EntityOrder.TargetPosition,
                                Footprint, -1, -1))
                        {
                            EntityOrder.TargetPosition = InOrder.TargetPosition;
                        }
                    }
                }
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
    IssueMove(EM, Entities, Target, PathingIgnoreEntityID, false);
}

void CommandSystem::IssueMove(Core::ECS::EntityManager& EM,
                               const std::vector<Core::ECS::EntityID>& Entities,
                               const Core::Math::Vec2& Target,
                               int PathingIgnoreEntityID,
                               bool Queued)
{
    if (!Queued)
    {
        for (Core::ECS::EntityID EntityID : Entities)
            InterruptHarvest(EM, EntityID);
    }

    Order MoveOrder;
    MoveOrder.Type = OrderType::Move;
    MoveOrder.TargetPosition = Target;
    MoveOrder.PathingIgnoreEntityID = PathingIgnoreEntityID;
    IssueOrder(EM, Entities, MoveOrder, Queued);
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
        if (auto* Path = EM.GetComponent<PathfindingComponent>(EntityID))
        {
            Path->FixedPosition = Core::Math::FixedVec2(
                Harvest->GasExitPosition.X, Harvest->GasExitPosition.Y);
            Path->FixedVelocity = Core::Math::FixedVec2::Zero();
            Path->HasFixedPosition = true;
            Path->RequestedDestination = Harvest->GasExitPosition;
        }
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
    IssueAttackMove(EM, Units, Target, false);
}

void CommandSystem::IssueAttackMove(Core::ECS::EntityManager& EM,
                                     const std::vector<int>& Units,
                                     Core::Math::Vec2 Target,
                                     bool Queued)
{
    if (!Queued)
    {
        for (int EntityID : Units)
            InterruptHarvest(EM, EntityID);
    }
    Order AtkOrder;
    AtkOrder.Type = OrderType::AttackMove;
    AtkOrder.TargetPosition = Target;
    IssueOrder(EM, Units, AtkOrder, Queued);
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
    Cmd->RallyDisplayTimer = 1.5f;
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
