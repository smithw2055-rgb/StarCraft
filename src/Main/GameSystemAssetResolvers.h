#pragma once

#include <cstdint>
#include <functional>
#include <utility>

#include "Game/Systems/BuildingSystem.h"
#include "Game/Systems/ProductionSystem.h"

namespace Game::Client::GameLoop
{

inline void ConfigureBuildAndProductionImageResolvers(std::function<uint32_t(int)> Resolver)
{
    Game::Systems::ProductionSystem::SetImageResolver(Resolver);
    Game::Systems::BuildingSystem::SetImageResolver(std::move(Resolver));
}

} // namespace Game::Client::GameLoop
