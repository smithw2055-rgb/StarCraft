#include "Game/Systems/CommandSystem.h"
#include "Game/Components.h"
#include "Game/Pathfinding/AStar.h"
#include "Game/Pathfinding/RegionPathfinder.h"
#include "Game/Map/Map.h"
#include "Game/Systems/HarvestSystem.h"
#include "Game/Systems/CombatSystem.h"
#include "Game/Systems/BuildingSystem.h"
#include "Game/Systems/UpgradeSystem.h"
#include "Game/DataEnums.h"
#include "Game/Races/RaceDef.h"

#include <algorithm>
#include <functional>
#include <vector>

namespace Game::Systems
{

using namespace Game::Components;

const Game::Map::GameMap* gCmdMap = nullptr;
static Game::Pathfinding::RegionPathfinder sRegionPathfinder;

void CommandSystem::SetMap(const Game::Map::GameMap* Map)
{
    gCmdMap = Map;

    // Build region pathfinder from current map walkability.
    if (Map)
    {
        Game::Pathfinding::RegionPathfinder::MapContext Ctx;
        Ctx.MapWidth = Map->GetWidth();
        Ctx.MapHeight = Map->GetHeight();

        for (int y = 0; y < Ctx.MapHeight; ++y)
            for (int x = 0; x < Ctx.MapWidth; ++x)
            {
                Ctx.Walkable[x][y] = Map->IsWalkable(x, y);
                Ctx.TileRegion[x][y] = -1;
            }

        sRegionPathfinder.BuildRegions(Ctx);
    }
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
           Type == OrderType::Guard;
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

    if (IsFrozenByStatus(EM, EntityID))
        return false;

    if (EM.GetComponent<ResourceComponent>(EntityID))
        return false;

    if (auto* Building = EM.GetComponent<BuildingComponent>(EntityID))
        return Building->IsLifted;

    if (IsSiegedTankUnit(Unit->ChkUnitID))
        return false;

    return Unit->Speed > 0.0f;
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
        return !Building->IsLifted;
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

static std::function<bool(int, int)> BuildStaticBlockerTilePredicate(
    Core::ECS::EntityManager& EM,
    Core::ECS::EntityID MovingID,
    const Game::Map::GameMap& Map,
    int StartTileX,
    int StartTileY,
    float MovingRadius,
    int IgnoreEntityID = -1)
{
    std::vector<StaticBlockerRect> Blockers;
    for (auto EntityID : EM.GetEntitiesWith<TransformComponent, UnitComponent>())
    {
        if (EntityID == IgnoreEntityID)
            continue;
        if (EntityID == MovingID || !IsStaticGroundBlocker(EM, EntityID))
            continue;
        auto Rect = StaticBlockerRectForEntity(EM, EntityID);
        Rect.Left += MovingRadius;
        Rect.Top += MovingRadius;
        Rect.Right += MovingRadius;
        Rect.Bottom += MovingRadius;
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
        return;
    }

    if (Path->IsAir)
    {
        Path->Path.push_back(InOrder.TargetPosition);
    }
    else if (gCmdMap)
    {
        auto* Trans = EM.GetComponent<TransformComponent>(EntityID);
        if (Trans)
        {
            int StartTileX = 0;
            int StartTileY = 0;
            int EndTileX = 0;
            int EndTileY = 0;
            gCmdMap->WorldToTile(Trans->Position, StartTileX, StartTileY);
            gCmdMap->WorldToTile(InOrder.TargetPosition, EndTileX, EndTileY);
            const auto ExtraBlockedTile = BuildStaticBlockerTilePredicate(
                EM,
                EntityID,
                *gCmdMap,
                StartTileX,
                StartTileY,
                (float)std::max(4, Path->CollisionRadius),
                InOrder.PathingIgnoreEntityID);
            Core::Math::Vec2 StageTarget = InOrder.TargetPosition;
            const auto RegionPath = sRegionPathfinder.FindRegionPath(
                StartTileX, StartTileY, EndTileX, EndTileY);
            if (RegionPath.Found && RegionPath.RegionSequence.size() > 1)
            {
                const int NextRegionID = RegionPath.RegionSequence[1];
                if (const auto* NextRegion = sRegionPathfinder.GetRegion(NextRegionID))
                {
                    StageTarget = gCmdMap->TileToWorld(
                        NextRegion->CenterX, NextRegion->CenterY);
                }
            }
            auto Result = Game::Pathfinding::AStar::FindPath(*gCmdMap,
                Trans->Position, StageTarget,
                1000, TerrainClearanceForPath(*Path), ExtraBlockedTile);
            if ((!Result.Found || Result.Waypoints.empty()) &&
                (StageTarget - InOrder.TargetPosition).LengthSq() > 1.0f)
            {
                Result = Game::Pathfinding::AStar::FindPath(
                    *gCmdMap,
                    Trans->Position,
                    InOrder.TargetPosition,
                    1000,
                    TerrainClearanceForPath(*Path),
                    ExtraBlockedTile);
            }
            if (Result.Found && !Result.Waypoints.empty())
                Path->Path = std::move(Result.Waypoints);
        }
        else
        {
            Path->Path.clear();
        }
    }
    else
    {
        Path->Path.push_back(InOrder.TargetPosition);
    }
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
                // Repair target: move near friendly mechanical unit/building
                if (Order->TargetEntityID >= 0)
                {
                    auto* TargetTrans = EM.GetComponent<TransformComponent>(Order->TargetEntityID);
                    auto* TargetUnit = EM.GetComponent<UnitComponent>(Order->TargetEntityID);
                    auto* TargetPlayer = EM.GetComponent<PlayerComponent>(Order->TargetEntityID);
                    auto* MyPlayer = EM.GetComponent<PlayerComponent>(ID);

                    if (TargetTrans && TargetUnit && TargetPlayer && MyPlayer &&
                        TargetPlayer->PlayerID == MyPlayer->PlayerID &&
                        TargetUnit->HitPoints > 0 &&
                        TargetUnit->HitPoints < TargetUnit->MaxHitPoints)
                    {
                        auto* MyTrans = EM.GetComponent<TransformComponent>(ID);
                        if (MyTrans)
                        {
                            float D = MyTrans->Position.DistanceTo(TargetTrans->Position);
                            if (D > 48.0f)
                            {
                                // Move toward repair target
                                auto* Path = EM.GetComponent<PathfindingComponent>(ID);
                                if (!Path)
                                    Path = &EM.AddComponent<PathfindingComponent>(ID);
                                Path->Path.clear();
                                Path->PathIndex = 0;

                                auto Result = Game::Pathfinding::AStar::FindPath(
                                    *gCmdMap, MyTrans->Position, TargetTrans->Position,
                                    1000, TerrainClearanceForPath(*Path));
                                if (Result.Found && !Result.Waypoints.empty())
                                    Path->Path = std::move(Result.Waypoints);
                                else
                                    Path->Path.push_back(TargetTrans->Position);
                            }
                            else
                            {
                                // In range: repair
                                int RepairAmount = std::min(10, TargetUnit->MaxHitPoints - TargetUnit->HitPoints);
                                TargetUnit->HitPoints = std::min(TargetUnit->MaxHitPoints,
                                    TargetUnit->HitPoints + RepairAmount);
                            }
                        }
                    }
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
        Core::Math::Vec2 Center;
        Core::Math::Vec2 Min;
        Core::Math::Vec2 Max;
        int Count = 0;
        bool AnyGroundUnit = false;
        bool TargetInsideBounds = false;
        bool PreserveOffsets = false;
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
        }

        if (Group.Count >= 2)
        {
            Group.Center = Group.Center / (float)Group.Count;
            Group.TargetInsideBounds =
                InOrder.TargetPosition.X >= Group.Min.X &&
                InOrder.TargetPosition.X <= Group.Max.X &&
                InOrder.TargetPosition.Y >= Group.Min.Y &&
                InOrder.TargetPosition.Y <= Group.Max.Y;
            const float MaxSpan = Group.AnyGroundUnit ? 192.0f : 256.0f;
            Group.PreserveOffsets =
                !Group.TargetInsideBounds &&
                Group.Max.X - Group.Min.X <= MaxSpan &&
                Group.Max.Y - Group.Min.Y <= MaxSpan;
        }
    }

    for (Core::ECS::EntityID ID : Entities)
    {
        Order EntityOrder = InOrder;
        if (AdjustGroupPositions && Group.Count >= 2)
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
                    EntityOrder.TargetPosition =
                        Trans->Position + (InOrder.TargetPosition - Group.Center);
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
    Order MoveOrder;
    MoveOrder.Type = OrderType::Move;
    MoveOrder.TargetPosition = Target;
    MoveOrder.PathingIgnoreEntityID = PathingIgnoreEntityID;
    IssueOrder(EM, Entities, MoveOrder, false);
}

bool CommandSystem::IssueMoveAndHasPath(Core::ECS::EntityManager& EM,
                                        Core::ECS::EntityID EntityID,
                                        const Core::Math::Vec2& Target,
                                        int PathingIgnoreEntityID)
{
    IssueMove(EM, {EntityID}, Target, PathingIgnoreEntityID);

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
