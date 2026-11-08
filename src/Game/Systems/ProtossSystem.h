#pragma once

// === Consolidated from src/Game/ProtossPower.h ===
namespace Game::ProtossPower
{

inline constexpr int PylonRadiusTiles = 6;
inline constexpr float TileSize = 32.0f;
inline constexpr float PylonRadiusWorld =
    (float)PylonRadiusTiles * TileSize;

inline bool ContainsTileOffset(int Dx, int Dy)
{
    return Dx * Dx + Dy * Dy <= PylonRadiusTiles * PylonRadiusTiles;
}

} // namespace Game::ProtossPower

// === End src/Game/ProtossPower.h ===

#include <vector>
#include <cstdint>

#include "Core/ECS/SystemManager.h"

namespace Game::Map { class GameMap; }

namespace Game::Systems
{

// World-scoped Protoss Pylon Power Grid stored inside EntityManager::Context()
struct ProtossPowerGridResource
{
    std::vector<uint8_t> PowerGrid;
    int Width = 0;
    int Height = 0;

    bool HasPower(int TileX, int TileY) const
    {
        if (TileX < 0 || TileX >= Width || TileY < 0 || TileY >= Height)
            return false;
        return PowerGrid[static_cast<size_t>(TileY) * static_cast<size_t>(Width) + static_cast<size_t>(TileX)] != 0;
    }
};

class ProtossSystem : public Core::ECS::ISystem
{
public:
    ProtossSystem(Core::ECS::SystemManager& SysMgr, Game::Map::GameMap* Map);
    void Update(float DeltaTime) override;
    std::string_view GetName() const override { return "ProtossSystem"; }

    bool HasPower(int TileX, int TileY) const;
    static bool HasPower(const Core::ECS::EntityManager& EM, int TileX, int TileY);
    static void SetMap(Game::Map::GameMap* Map);

    static bool MorphArchon(Core::ECS::EntityManager& EM,
                             int TemplarAID, int TemplarBID,
                             bool IsDarkArchon,
                             bool InitialShieldCharge = false);
    static bool StartArchonWarp(Core::ECS::EntityManager& EM,
                                int TemplarAID, int TemplarBID,
                                bool IsDarkArchon,
                                float Duration = 4.0f,
                                bool InitialShieldCharge = true);
    static bool CanRechargeShieldsTarget(Core::ECS::EntityManager& EM,
                                         int BatteryID,
                                         int TargetID);
    static bool RechargeShieldsTarget(Core::ECS::EntityManager& EM,
                                      int BatteryID,
                                      int TargetID);

private:
    const Game::Map::GameMap* ResolveMap(const Core::ECS::EntityManager& EM) const;
    void UpdatePowerGrid(Core::ECS::EntityManager& EM);
    void RechargeShields(Core::ECS::EntityManager& EM, float DeltaTime);
    void CheckPowerStatus(Core::ECS::EntityManager& EM);
    void UpdateArchonWarps(Core::ECS::EntityManager& EM, float DeltaTime);

    Core::ECS::SystemManager& mSystemMgr;
    Game::Map::GameMap* mInstanceMap = nullptr;
    static Game::Map::GameMap* sMap;

    std::vector<bool> mPowerGrid;
    int mPowerGridW = 0;
    int mPowerGridH = 0;
};

} // namespace Game::Systems
