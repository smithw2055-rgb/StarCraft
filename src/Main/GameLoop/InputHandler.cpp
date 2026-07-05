#include "InputHandler.h"
#include "CursorTypes.h"
#include "Game/Components.h"
#include "Game/Systems/CommandSystem.h"
#include "Core/Math/Vec2.h"

namespace Game::Client::GameLoop
{

void IssueMoveCommand(Core::ECS::EntityManager& EM,
                      const std::vector<Core::ECS::EntityID>& EcsIds,
                      const Core::Math::Vec2& Target)
{
    Game::Systems::CommandSystem::IssueMove(EM, EcsIds, Target);
}

void IssuePatrolCommand(Core::ECS::EntityManager& EM,
                        const std::vector<Core::ECS::EntityID>& EcsIds,
                        const Core::Math::Vec2& Target)
{
    Game::Components::Order PatrolOrder;
    PatrolOrder.Type = Game::Components::OrderType::Patrol;
    PatrolOrder.TargetPosition = Target;
    Game::Systems::CommandSystem::IssueOrder(EM, EcsIds, PatrolOrder, false);
}

void IssueFollowCommand(Core::ECS::EntityManager& EM,
                        const std::vector<Core::ECS::EntityID>& EcsIds,
                        int TargetEntityId)
{
    if (TargetEntityId < 0) return;
    auto* FollowTarget = EM.GetComponent<Game::Components::TransformComponent>(TargetEntityId);
    if (!FollowTarget)
        return;

    Game::Components::Order FollowOrder;
    FollowOrder.Type = Game::Components::OrderType::Follow;
    FollowOrder.TargetPosition = FollowTarget->Position;
    FollowOrder.TargetEntityID = TargetEntityId;
    Game::Systems::CommandSystem::IssueOrder(EM, EcsIds, FollowOrder, false);
    for (int EntityId : EcsIds)
    {
        auto* Command = EM.GetComponent<Game::Components::CommandComponent>(EntityId);
        if (Command && Command->CurrentCommand == Game::Components::CommandType::Follow)
            Command->TargetEntityID = TargetEntityId;
    }
}

void SetAllCommand(Core::ECS::EntityManager& EM,
                   const std::vector<Core::ECS::EntityID>& EcsIds,
                   Game::Components::CommandType Cmd)
{
    for (int Eid : EcsIds)
    {
        auto* C = EM.GetComponent<Game::Components::CommandComponent>(Eid);
        if (C)
        {
            C->CancelAll();
            C->CurrentCommand = Cmd;
            C->TargetEntityID = -1;
            C->TargetPosition = {};
        }
        auto* P = EM.GetComponent<Game::Components::PathfindingComponent>(Eid);
        if (P)
        {
            P->Path.clear();
            P->PathIndex = 0;
            P->CurrentSpeed = 0.0f;
            P->CurrentVelocity = {0.0f, 0.0f};
        }
        auto* Combat = EM.GetComponent<Game::Components::CombatComponent>(Eid);
        if (Combat)
        {
            Combat->TargetEntityID = -1;
            Combat->PendingAttackFrame = false;
            Combat->PendingAttackTargetEntityID = -1;
            Combat->PendingAttackTargetPos = {};
            Combat->PendingAttackElapsed = 0.0f;
        }
    }
}

} // namespace Game::Client::GameLoop
