#pragma once

#include <vector>
#include <cstdint>

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

    bool HasPower(int TileX, int TileY);
    static void SetMap(Game::Map::GameMap* Map);

    static bool MorphArchon(Core::ECS::EntityManager& EM,
                             int TemplarAID, int TemplarBID,
                             bool IsDarkArchon);

private:
    void UpdatePowerGrid(Core::ECS::EntityManager& EM);
    void RechargeShields(Core::ECS::EntityManager& EM, float DeltaTime);
    void CheckPowerStatus(Core::ECS::EntityManager& EM);

    Core::ECS::SystemManager& mSystemMgr;
    static Game::Map::GameMap* sMap;

    std::vector<bool> mPowerGrid;
    int mPowerGridW = 0;
    int mPowerGridH = 0;
};

} // namespace Game::Systems
