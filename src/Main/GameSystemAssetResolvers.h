#pragma once

#include <cstdint>
#include <functional>
#include <utility>

#include "Game/Systems/AnimationSystem.h"
#include "Game/Systems/BulletSystem.h"
#include "Game/Systems/BuildingSystem.h"
#include "Game/Systems/CommandSystem.h"
#include "Game/Systems/NukeAndSpiderMineSystem.h"
#include "Game/Systems/ProductionSystem.h"
#include "Game/Systems/SpellSystem.h"
#include "Game/Systems/SystemAssetBridge.h"
#include "Game/Systems/ZergSystem.h"

namespace Game::Client::GameLoop
{

inline void ConfigureModernAssetRegistryResolvers(const Resource::ModernAssetRegistry& Registry)
{
    Game::Systems::SystemAssetBridge::BindModernAssetRegistry(Registry);
}

inline void ConfigureBuildAndProductionImageResolvers(std::function<uint32_t(int)> Resolver)
{
    Game::Systems::ProductionSystem::SetImageResolver(Resolver);
    Game::Systems::BuildingSystem::SetImageResolver(Resolver);
    Game::Systems::CommandSystem::SetImageResolver(Resolver);
    Game::Systems::NukeAndSpiderMineSystem::SetImageResolver(Resolver);
    Game::Systems::SpellSystem::SetImageResolver(Resolver);
    Game::Systems::ZergSystem::SetImageResolver(std::move(Resolver));
}

inline void ConfigureBuildAndProductionImageResolvers(std::function<uint32_t(int)> Resolver,
                                                      std::function<uint32_t(int)> ConstructionResolver)
{
    ConfigureBuildAndProductionImageResolvers( std::move(Resolver) );
    Game::Systems::BuildingSystem::SetConstructionImageResolver(std::move(ConstructionResolver));
}

inline void ConfigureBuildAndProductionSubunitImageResolvers(
    std::function<uint32_t(int)> Resolver)
{
    Game::Systems::ProductionSystem::SetSubunitImageResolver(Resolver);
    Game::Systems::BuildingSystem::SetSubunitImageResolver(Resolver);
    Game::Systems::CommandSystem::SetSubunitImageResolver(std::move(Resolver));
}

inline void ConfigureGameplayCollisionBoundsResolvers(
    std::function<Game::Components::UnitCollisionBounds(int)> Resolver)
{
    Game::Systems::ProductionSystem::SetCollisionBoundsResolver(Resolver);
    Game::Systems::BuildingSystem::SetCollisionBoundsResolver(Resolver);
    Game::Systems::ZergSystem::SetCollisionBoundsResolver(std::move(Resolver));
}

inline void ConfigureBuildingAddonOffsetResolver(
    Game::Systems::BuildingSystem::AddonOffsetResolver Resolver)
{
    Game::Systems::BuildingSystem::SetAddonOffsetResolver(std::move(Resolver));
}

inline void ConfigureAnimationOverlayOffsetResolver(
    Game::Systems::AnimationSystem::OverlayOffsetResolver Resolver)
{
    Game::Systems::AnimationSystem::SetOverlayOffsetResolver(std::move(Resolver));
}

inline void ConfigureAnimationWeaponImageResolver(
    Game::Systems::AnimationSystem::WeaponImageResolver Resolver)
{
    Game::Systems::AnimationSystem::SetWeaponImageResolver(std::move(Resolver));
}

inline void ConfigureAnimationSpriteImageResolver(
    Game::Systems::AnimationSystem::SpriteImageResolver Resolver)
{
    Game::Systems::AnimationSystem::SetSpriteImageResolver(std::move(Resolver));
}

inline void ConfigureAnimationImageAttachmentPersistenceResolver(
    Game::Systems::AnimationSystem::ImageAttachmentPersistenceResolver Resolver)
{
    Game::Systems::AnimationSystem::SetImageAttachmentPersistenceResolver(std::move(Resolver));
}

inline void ConfigureZergBurrowCapabilityResolver(
    Game::Systems::ZergSystem::BurrowCapabilityResolver Resolver)
{
    Game::Systems::ZergSystem::SetBurrowCapabilityResolver(std::move(Resolver));
}

inline void ConfigureBulletVisualResolver(
    Game::Systems::BulletSystem::VisualResolver Resolver)
{
    Game::Systems::BulletSystem::SetVisualResolver(std::move(Resolver));
}

} // namespace Game::Client::GameLoop
