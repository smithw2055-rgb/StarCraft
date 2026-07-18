#pragma once

#include "Core/ECS/SystemManager.h"
#include "Core/Math/Vec2.h"

namespace Game::Map
{
class GameMap;
}

namespace Game::Systems
{

class MovementSystem : public Core::ECS::ISystem
{
public:
    using PathRequestCallback = bool (*)(
        Core::ECS::EntityManager&,
        Core::ECS::EntityID,
        const Game::Map::GameMap*,
        const Core::Math::Vec2&,
        int,
        int);

    MovementSystem(Core::ECS::SystemManager& SysMgr) : mSystemMgr(SysMgr) {}
    MovementSystem(Core::ECS::SystemManager& SysMgr, const Game::Map::GameMap* Map)
        : mSystemMgr(SysMgr), mMap(Map) {}
    void Update(float DeltaTime) override;
    std::string_view GetName() const override { return "MovementSystem"; }
    void SetMap(const Game::Map::GameMap* Map) { mMap = Map; }
    static void SetPathRequestCallback(PathRequestCallback Callback)
    {
        sPathRequestCallback = Callback;
    }
    static PathRequestCallback GetPathRequestCallback()
    {
        return sPathRequestCallback;
    }

private:
    inline static PathRequestCallback sPathRequestCallback = nullptr;
    Core::ECS::SystemManager& mSystemMgr;
    const Game::Map::GameMap* mMap = nullptr;
};

} // namespace Game::Systems
