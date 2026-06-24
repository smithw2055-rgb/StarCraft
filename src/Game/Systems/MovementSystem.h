#pragma once

#include "Core/ECS/SystemManager.h"

namespace Resource { class ResourceCache; }

namespace Game::Systems
{

class MovementSystem : public Core::ECS::ISystem
{
public:
    MovementSystem(Core::ECS::SystemManager& SysMgr) : mSystemMgr(SysMgr) {}
    void Update(float DeltaTime) override;
    std::string_view GetName() const override { return "MovementSystem"; }

    static void SetAnimCache(Resource::ResourceCache* Cache);

private:
    Core::ECS::SystemManager& mSystemMgr;
};

} // namespace Game::Systems
