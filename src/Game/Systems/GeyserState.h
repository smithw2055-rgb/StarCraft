#pragma once

#include "Core/ECS/EntityManager.h"
#include "Game/Components.h"

namespace Game::Systems
{

inline bool IsAttackableClaimedGeyser(Core::ECS::EntityManager& EM, int EntityID)
{
    const auto* Resource =
        EM.GetComponent<Game::Components::ResourceComponent>(EntityID);
    const auto* Claimed =
        EM.GetComponent<Game::Components::ClaimedGeyserComponent>(EntityID);
    const auto* Building =
        EM.GetComponent<Game::Components::BuildingComponent>(EntityID);
    return Resource &&
           Resource->Type == Game::Components::ResourceType::VespeneGas &&
           Claimed && Claimed->IsClaimed && Building &&
           Building->BuildingDefID >= 0;
}

inline bool IsProtectedResourceNode(Core::ECS::EntityManager& EM, int EntityID)
{
    return EM.GetComponent<Game::Components::ResourceComponent>(EntityID) &&
           !IsAttackableClaimedGeyser(EM, EntityID);
}

inline bool IsMeleeStructureEntity(Core::ECS::EntityManager& EM, int EntityID)
{
    const auto* Unit =
        EM.GetComponent<Game::Components::UnitComponent>(EntityID);
    const auto* Building =
        EM.GetComponent<Game::Components::BuildingComponent>(EntityID);
    return Unit && Unit->HitPoints > 0 && Building &&
           Building->BuildingDefID >= 0;
}

} // namespace Game::Systems
