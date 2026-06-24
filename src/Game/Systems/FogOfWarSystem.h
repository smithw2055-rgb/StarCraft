#pragma once

#include <vector>
#include <cstdint>

#include "Core/ECS/SystemManager.h"

namespace Game::Map { class GameMap; }

namespace Game::Systems
{

enum class FogState : uint8_t
{
    Unexplored = 0,
    Explored,
    Visible
};

class FogOfWarSystem : public Core::ECS::ISystem
{
public:
    FogOfWarSystem(Core::ECS::SystemManager& SysMgr, const Game::Map::GameMap* Map);
    void Update(float DeltaTime) override;
    std::string_view GetName() const override { return "FogOfWarSystem"; }

    FogState GetFog(int TileX, int TileY, int PlayerID = 0) const;
    bool IsVisible(int TileX, int TileY, int PlayerID = 0) const;
    bool IsExplored(int TileX, int TileY, int PlayerID = 0) const;

    int GetWidth() const { return mMapWidth; }
    int GetHeight() const { return mMapHeight; }

private:
    void RevealAround(int CenterX, int CenterY, int SightRange, uint8_t* Grid);
    void ResetVisibility();

    Core::ECS::SystemManager& mSystemMgr;
    const Game::Map::GameMap* mMap = nullptr;
    int mMapWidth = 0;
    int mMapHeight = 0;

    // Per-player fog grids: 0=Unexplored, 1=Explored, 2=Visible
    // Index: [PlayerID * Width * Height + Y * Width + X]
    std::vector<uint8_t> mFogGrid;
    int mMaxPlayers = 8;
};

} // namespace Game::Systems
