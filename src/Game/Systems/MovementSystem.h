#pragma once

#include "Core/ECS/SystemManager.h"

namespace Game::Map
{
class GameMap;
}

namespace Game::Systems
{

class MovementSystem : public Core::ECS::ISystem
{
public:
    MovementSystem(Core::ECS::SystemManager& SysMgr) : mSystemMgr(SysMgr) {}
    MovementSystem(Core::ECS::SystemManager& SysMgr, const Game::Map::GameMap* Map)
        : mSystemMgr(SysMgr), mMap(Map) {}
    void Update(float DeltaTime) override;
    std::string_view GetName() const override { return "MovementSystem"; }
    void SetMap(const Game::Map::GameMap* Map) { mMap = Map; }

private:
    Core::ECS::SystemManager& mSystemMgr;
    const Game::Map::GameMap* mMap = nullptr;
};

} // namespace Game::Systems
