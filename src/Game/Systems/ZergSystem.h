#pragma once

#include <vector>
#include <cstdint>

#include "Core/ECS/SystemManager.h"
#include "Core/Math/Vec2.h"

namespace Game::Map { class GameMap; }

namespace Game::Systems
{

class ZergSystem : public Core::ECS::ISystem
{
public:
    ZergSystem(Core::ECS::SystemManager& SysMgr, Game::Map::GameMap* Map);
    void Update(float DeltaTime) override;
    std::string_view GetName() const override { return "ZergSystem"; }

    static bool HasCreep(int TileX, int TileY);
    static void SetMap(Game::Map::GameMap* Map);

private:
    void SpreadCreep(Core::ECS::EntityManager& EM);
    void ProcessLarvae(Core::ECS::EntityManager& EM, float DeltaTime);

    Core::ECS::SystemManager& mSystemMgr;
    static Game::Map::GameMap* sMap;

    // Creep grid: separate from GameMap for simplicity
    // Index: Y * Width + X
    std::vector<uint8_t> mCreepGrid;
    int mMapWidth = 0;
    int mMapHeight = 0;
    float mCreepTimer = 0.0f;
    float mCreepInterval = 1.0f;
    bool mGridInitialized = false;
};

} // namespace Game::Systems