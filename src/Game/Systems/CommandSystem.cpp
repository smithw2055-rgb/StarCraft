#include "Game/Systems/CommandSystem.h"
#include "Game/Components.h"
#include "Game/Pathfinding/AStar.h"
#include "Game/Map/Map.h"

namespace Game::Systems
{

const Game::Map::GameMap* gCmdMap = nullptr;

void CommandSystem::SetMap(const Game::Map::GameMap* Map)
{
    gCmdMap = Map;
}

void CommandSystem::Update(float DeltaTime)
{
    // Future: process command queues for Attack, Harvest, Build, etc.
}

void CommandSystem::IssueMove(Core::ECS::EntityManager& EM,
                              const std::vector<Core::ECS::EntityID>& Entities,
                              const Core::Math::Vec2& Target)
{
    for (Core::ECS::EntityID ID : Entities)
    {
        auto* Path = EM.GetComponent<Game::Components::PathfindingComponent>(ID);
        if (!Path)
            Path = &EM.AddComponent<Game::Components::PathfindingComponent>(ID);

        Path->Path.clear();
        Path->PathIndex = 0;
        Path->NodeTolerance = 8.0f;

        if (gCmdMap)
        {
            auto* Trans = EM.GetComponent<Game::Components::TransformComponent>(ID);
            if (Trans)
            {
                auto Result = Game::Pathfinding::AStar::FindPath(*gCmdMap, Trans->Position, Target);
                if (Result.Found && !Result.Waypoints.empty())
                {
                    Path->Path = std::move(Result.Waypoints);
                }
                else
                {
                    Path->Path.push_back(Target);
                }
            }
            else
            {
                Path->Path.push_back(Target);
            }
        }
        else
        {
            Path->Path.push_back(Target);
        }

        auto* Cmd = EM.GetComponent<Game::Components::CommandComponent>(ID);
        if (!Cmd)
            Cmd = &EM.AddComponent<Game::Components::CommandComponent>(ID);
        Cmd->CurrentCommand = Game::Components::CommandType::Move;
        Cmd->TargetPosition = Target;
    }
}

} // namespace Game::Systems
