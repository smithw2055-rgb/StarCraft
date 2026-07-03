#pragma once

#include <cstdint>
#include <vector>
#include "Core/ECS/EntityManager.h"

namespace Core::Math { struct Vec2; }
namespace Game::Components { enum class CommandType : uint8_t; }

namespace Game::Client::GameLoop
{

void IssueMoveCommand(Core::ECS::EntityManager& EM,
                      const std::vector<Core::ECS::EntityID>& EcsIds,
                      const Core::Math::Vec2& Target);

void IssuePatrolCommand(Core::ECS::EntityManager& EM,
                        const std::vector<Core::ECS::EntityID>& EcsIds,
                        const Core::Math::Vec2& Target);

void IssueFollowCommand(Core::ECS::EntityManager& EM,
                        const std::vector<Core::ECS::EntityID>& EcsIds,
                        int TargetEntityId);

void SetAllCommand(Core::ECS::EntityManager& EM,
                   const std::vector<Core::ECS::EntityID>& EcsIds,
                   Game::Components::CommandType Cmd);

} // namespace Game::Client::GameLoop
