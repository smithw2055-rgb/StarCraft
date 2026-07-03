#pragma once

#include "Core/ECS/SystemManager.h"

namespace Game::Systems
{

class MovementSystem : public Core::ECS::ISystem
{
public:
    MovementSystem(Core::ECS::SystemManager& SysMgr) : mSystemMgr(SysMgr) {}
    void Update(float DeltaTime) override;
    std::string_view GetName() const override { return "MovementSystem"; }

private:
    Core::ECS::SystemManager& mSystemMgr;
};

} // namespace Game::Systems
