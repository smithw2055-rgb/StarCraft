#pragma once

#include <cstdint>
#include <vector>
#include "Client/Selection.h"
#include "Core/ECS/EntityManager.h"

namespace Game::Client::GameLoop
{

inline std::vector<Core::ECS::EntityID> CollectSelectedAliveEcsIds(
    Core::ECS::EntityManager& EM,
    const std::vector<int>& SelectedIds)
{
    std::vector<Core::ECS::EntityID> EcsIds;
    EcsIds.reserve(SelectedIds.size());
    for (int SelectedId : SelectedIds)
    {
        if (EM.IsAlive(SelectedId))
            EcsIds.push_back(SelectedId);
    }
    return EcsIds;
}

inline std::vector<Core::ECS::EntityID> CollectSelectedAliveEcsIds(
    Core::ECS::EntityManager& EM,
    const Game::Client::Selection& Selector)
{
    return CollectSelectedAliveEcsIds(EM, Selector.GetSelected());
}

} // namespace Game::Client::GameLoop
