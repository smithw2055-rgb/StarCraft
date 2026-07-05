#pragma once

#include <cstdint>
#include <functional>
#include <utility>

#include "Game/Systems/BuildingSystem.h"
#include "Game/Systems/ProductionSystem.h"
#include "Game/Systems/ZergSystem.h"

namespace Game::Client::GameLoop
{

inline void ConfigureBuildAndProductionImageResolvers(std::function<uint32_t(int)> Resolver)
{
    Game::Systems::ProductionSystem::SetImageResolver(Resolver);
    Game::Systems::BuildingSystem::SetImageResolver(std::move(Resolver));
}

inline void ConfigureBuildAndProductionImageResolvers(std::function<uint32_t(int)> Resolver,
                                                      std::function<uint32_t(int)> ConstructionResolver)
{
    Game::Systems::ProductionSystem::SetImageResolver(Resolver);
    Game::Systems::BuildingSystem::SetImageResolver(Resolver);
    Game::Systems::BuildingSystem::SetConstructionImageResolver(std::move(ConstructionResolver));
}

inline void ConfigureGameplayCollisionBoundsResolvers(
    std::function<Game::Components::UnitCollisionBounds(int)> Resolver)
{
    Game::Systems::ProductionSystem::SetCollisionBoundsResolver(Resolver);
    Game::Systems::BuildingSystem::SetCollisionBoundsResolver(Resolver);
    Game::Systems::ZergSystem::SetCollisionBoundsResolver(std::move(Resolver));
}

} // namespace Game::Client::GameLoop
