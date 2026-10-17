#pragma once

#include "Core/ECS/SystemManager.h"
#include "Core/Math/Vec2.h"

#include <cstdint>

namespace Game::Map
{
class GameMap;
}

namespace Game::Systems
{

struct MovementNavigationStats
{
    uint64_t NeighborPairs = 0;
    uint64_t CandidatesRejected = 0;
    uint64_t StuckEvents = 0;
    uint64_t RepathRequests = 0;
};

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
    using NavigationSyncCallback = void (*)(
        Core::ECS::EntityManager&,
        const Game::Map::GameMap*);

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
    static void SetNavigationSyncCallback(NavigationSyncCallback Callback)
    {
        sNavigationSyncCallback = Callback;
    }
    static NavigationSyncCallback GetNavigationSyncCallback()
    {
        return sNavigationSyncCallback;
    }
    const MovementNavigationStats& GetLastNavigationStats() const
    {
        return mLastNavigationStats;
    }

private:
    inline static PathRequestCallback sPathRequestCallback = nullptr;
    inline static NavigationSyncCallback sNavigationSyncCallback = nullptr;
    Core::ECS::SystemManager& mSystemMgr;
    const Game::Map::GameMap* mMap = nullptr;
    MovementNavigationStats mLastNavigationStats;
};

} // namespace Game::Systems
