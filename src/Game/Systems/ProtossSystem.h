#pragma once

#include "Core/ECS/SystemManager.h"

namespace Game::Map { class GameMap; }

namespace Game::Systems
{

class ProtossSystem : public Core::ECS::ISystem
{
public:
    ProtossSystem(Core::ECS::SystemManager& SysMgr, Game::Map::GameMap* Map);
    void Update(float DeltaTime) override;
    std::string_view GetName() const override { return "ProtossSystem"; }

    static bool HasPower(int TileX, int TileY);
    static void SetMap(Game::Map::GameMap* Map);

private:
    void UpdatePowerGrid(Core::ECS::EntityManager& EM);
    void RechargeShields(Core::ECS::EntityManager& EM, float DeltaTime);

    Core::ECS::SystemManager& mSystemMgr;
    static Game::Map::GameMap* sMap;
};

} // namespace Game::Systems