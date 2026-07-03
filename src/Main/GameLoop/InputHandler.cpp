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
    for (int EntityId : EcsIds)
    {
        auto* Command = EM.GetComponent<Game::Components::CommandComponent>(EntityId);
        if (!Command)
            Command = &EM.AddComponent<Game::Components::CommandComponent>(EntityId);
        Command->TargetPosition = Target;
        auto* Path = EM.GetComponent<Game::Components::PathfindingComponent>(EntityId);
        if (Path) { Path->Path.clear(); Path->PathIndex = 0; }
    }
    Game::Systems::CommandSystem::IssueMove(EM, EcsIds, Target);
    for (int EntityId : EcsIds)
    {
        auto* Command = EM.GetComponent<Game::Components::CommandComponent>(EntityId);
        if (Command) Command->CurrentCommand = Game::Components::CommandType::Patrol;
    }
}

void IssueFollowCommand(Core::ECS::EntityManager& EM,
                        const std::vector<Core::ECS::EntityID>& EcsIds,
                        int TargetEntityId)
{
    if (TargetEntityId < 0) return;
    auto* FollowTarget = EM.GetComponent<Game::Components::TransformComponent>(TargetEntityId);
    if (FollowTarget)
        Game::Systems::CommandSystem::IssueMove(EM, EcsIds, FollowTarget->Position);
    for (int EntityId : EcsIds)
    {
        auto* Command = EM.GetComponent<Game::Components::CommandComponent>(EntityId);
        if (!Command)
            Command = &EM.AddComponent<Game::Components::CommandComponent>(EntityId);
        Command->CurrentCommand = Game::Components::CommandType::Follow;
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
        if (C) { C->CurrentCommand = Cmd; C->TargetEntityID = -1; }
        auto* P = EM.GetComponent<Game::Components::PathfindingComponent>(Eid);
        if (P) { P->Path.clear(); P->PathIndex = 0; }
    }
}

} // namespace Game::Client::GameLoop
