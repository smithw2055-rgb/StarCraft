#include "EcsSync.h"

#include "Client/Selection.h"

namespace Game::Client::GameLoop
{

std::vector<Core::ECS::EntityID> CollectSelectedAliveEcsIds(
    Core::ECS::EntityManager& EM, const Game::Client::Selection& Selector)
{
    return CollectSelectedAliveEcsIds(EM, Selector.GetSelected());
}

std::vector<Core::ECS::EntityID> CollectSelectedAliveEcsIds(
    Core::ECS::EntityManager& EM, const std::vector<int>& SelectedIds)
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

} // namespace Game::Client::GameLoop
