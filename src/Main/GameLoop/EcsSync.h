#pragma once

#include <cstdint>
#include <vector>
#include "Core/ECS/EntityManager.h"

namespace Game::Client { class Selection; }

namespace Game::Client::GameLoop
{

std::vector<Core::ECS::EntityID> CollectSelectedAliveEcsIds(
    Core::ECS::EntityManager& EM,
    const Game::Client::Selection& Selector);

std::vector<Core::ECS::EntityID> CollectSelectedAliveEcsIds(
    Core::ECS::EntityManager& EM,
    const std::vector<int>& SelectedIds);

} // namespace Game::Client::GameLoop
