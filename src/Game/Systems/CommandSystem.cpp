#include "Game/Systems/CommandSystem.h"
#include "Game/Components.h"
#include "Game/Pathfinding/AStar.h"
#include "Game/Pathfinding/RegionPathfinder.h"
#include "Game/Map/Map.h"
#include "Game/Systems/HarvestSystem.h"
#include "Game/Systems/CombatSystem.h"
#include "Game/Systems/BuildingSystem.h"

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
                                    *gCmdMap, MyTrans->Position, TargetTrans->Position);
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

    // Pathfinding for movement-type orders
    if (InOrder.Type == OrderType::Move || InOrder.Type == OrderType::AttackMove ||
        InOrder.Type == OrderType::Patrol || InOrder.Type == OrderType::Follow ||
        InOrder.Type == OrderType::Guard)
    {
        auto* Path = EM.GetComponent<PathfindingComponent>(EntityID);
        if (!Path)
            Path = &EM.AddComponent<PathfindingComponent>(EntityID);
        Path->Path.clear();
        Path->PathIndex = 0;
        Path->NodeTolerance = 8.0f;

        if (gCmdMap)
        {
            auto* Trans = EM.GetComponent<TransformComponent>(EntityID);
            if (Trans)
            {
                // Use region pathfinder for long-distance paths
                auto Result = Game::Pathfinding::AStar::FindPath(*gCmdMap,
                    Trans->Position, InOrder.TargetPosition);
                if (Result.Found && !Result.Waypoints.empty())
                    Path->Path = std::move(Result.Waypoints);
                else
                    Path->Path.push_back(InOrder.TargetPosition);
            }
            else
            {
                Path->Path.push_back(InOrder.TargetPosition);
            }
        }
        else
        {
            Path->Path.push_back(InOrder.TargetPosition);
        }
    }
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

void CommandSystem::IssueAttackMove(Core::ECS::EntityManager& EM,
                                     const std::vector<int>& Units,
                                     Core::Math::Vec2 Target)
{
    for (int ID : Units)
    {
        auto* Cmd = EM.GetComponent<CommandComponent>(ID);
        if (!Cmd)
            Cmd = &EM.AddComponent<CommandComponent>(ID);
        Cmd->CurrentCommand = CommandType::AttackMove;

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
}

} // namespace Game::Systems
