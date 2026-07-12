#pragma once

#include <cstdint>
#include <functional>
#include <utility>

#include "Game/Systems/AnimationSystem.h"
#include "Game/Systems/BulletSystem.h"
#include "Game/Systems/BuildingSystem.h"
#include "Game/Systems/ProductionSystem.h"
#include "Game/Systems/ZergSystem.h"

namespace Game::Client::GameLoop
{

inline void ConfigureBuildAndProductionImageResolvers(std::function<uint32_t(int)> Resolver)
{
    Game::Systems::ProductionSystem::SetImageResolver(Resolver);
    Game::Systems::BuildingSystem::SetImageResolver(Resolver);
    Game::Systems::ZergSystem::SetImageResolver(std::move(Resolver));
}

inline void ConfigureBuildAndProductionImageResolvers(std::function<uint32_t(int)> Resolver,
                                                      std::function<uint32_t(int)> ConstructionResolver)
{
    Game::Systems::ProductionSystem::SetImageResolver(Resolver);
    Game::Systems::BuildingSystem::SetImageResolver(Resolver);
    Game::Systems::ZergSystem::SetImageResolver(Resolver);
    Game::Systems::BuildingSystem::SetConstructionImageResolver(std::move(ConstructionResolver));
}

inline void ConfigureGameplayCollisionBoundsResolvers(
    std::function<Game::Components::UnitCollisionBounds(int)> Resolver)
{
    Game::Systems::ProductionSystem::SetCollisionBoundsResolver(Resolver);
    Game::Systems::BuildingSystem::SetCollisionBoundsResolver(Resolver);
    Game::Systems::ZergSystem::SetCollisionBoundsResolver(std::move(Resolver));
}

inline void ConfigureAnimationOverlayOffsetResolver(
    Game::Systems::AnimationSystem::OverlayOffsetResolver Resolver)
{
    Game::Systems::AnimationSystem::SetOverlayOffsetResolver(std::move(Resolver));
}

inline void ConfigureBulletVisualResolver(
    Game::Systems::BulletSystem::VisualResolver Resolver)
{
    Game::Systems::BulletSystem::SetVisualResolver(std::move(Resolver));
}

} // namespace Game::Client::GameLoop
