#include "Game/Systems/CommandSystem.h"
#include "Game/Components.h"
#include "Game/Pathfinding/AStar.h"
#include "Game/Pathfinding/RegionPathfinder.h"
#include "Game/Map/Map.h"
#include "Game/Systems/HarvestSystem.h"
#include "Game/Systems/CombatSystem.h"
#include "Game/Systems/BuildingSystem.h"
#include "Game/DataEnums.h"

#include <algorithm>

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

static void ClearMovementPath(Core::ECS::EntityManager& EM, Core::ECS::EntityID EntityID)
{
    if (auto* Path = EM.GetComponent<PathfindingComponent>(EntityID))
    {
        Path->Path.clear();
        Path->PathIndex = 0;
        Path->CurrentSpeed = 0.0f;
        Path->CurrentVelocity = {0.0f, 0.0f};
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
    Path->NodeTolerance = 8.0f;
    RefreshCollisionRadius(EM, EntityID, *Path);
    Path->IsAir = ShouldMoveAsAir(EM, EntityID);

    if (Path->IsAir)
    {
        Path->Path.push_back(InOrder.TargetPosition);
    }
    else if (gCmdMap)
    {
        auto* Trans = EM.GetComponent<TransformComponent>(EntityID);
        if (Trans)
        {
            auto Result = Game::Pathfinding::AStar::FindPath(*gCmdMap,
                Trans->Position, InOrder.TargetPosition,
                1000, TerrainClearanceForPath(*Path));
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

    ConfigureMovementPathForOrder(EM, EntityID, InOrder);
}

void CommandSystem::IssueOrder(Core::ECS::EntityManager& EM,
                                const std::vector<Core::ECS::EntityID>& Entities,
                                const Order& InOrder,
                                bool Queued)
{
    for (Core::ECS::EntityID ID : Entities)
        IssueOrder(EM, ID, InOrder, Queued);
}

void CommandSystem::IssueMove(Core::ECS::EntityManager& EM,
                               const std::vector<Core::ECS::EntityID>& Entities,
                               const Core::Math::Vec2& Target)
{
    Order MoveOrder;
    MoveOrder.Type = OrderType::Move;
    MoveOrder.TargetPosition = Target;
    IssueOrder(EM, Entities, MoveOrder, false);
}

bool CommandSystem::IssueMoveAndHasPath(Core::ECS::EntityManager& EM,
                                        Core::ECS::EntityID EntityID,
                                        const Core::Math::Vec2& Target)
{
    IssueMove(EM, {EntityID}, Target);

    if (auto* Path = EM.GetComponent<PathfindingComponent>(EntityID))
        return !Path->Path.empty();

    return false;
}

void CommandSystem::IssueAttackMove(Core::ECS::EntityManager& EM,
                                     const std::vector<int>& Units,
                                     Core::Math::Vec2 Target)
{
    for (int ID : Units)
    {
        Order AtkOrder;
        AtkOrder.Type = OrderType::AttackMove;
        AtkOrder.TargetPosition = Target;
        IssueOrder(EM, {ID}, AtkOrder, false);
    }
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
        ConfigureMovementPathForOrder(EM, EntityID, *CurrentOrder);
}

} // namespace Game::Systems
