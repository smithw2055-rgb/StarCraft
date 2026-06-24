#pragma once

#include <vector>
#include <cstdint>

#include "Game/Components.h"
#include "Core/Math/Vec2.h"

namespace Game::Map
{

class ChkParser;

using Game::Components::TerrainType;
using Game::Components::ResourceType;

constexpr int DefaultMapWidth = 128;
constexpr int DefaultMapHeight = 128;
constexpr float TileSize = 32.0f;

struct Tile
{
    TerrainType Terrain = TerrainType::Grass;
    bool Walkable = true;
    bool Buildable = true;
    bool HasCreep = false;
    ResourceType Resource = ResourceType::None;
    int ResourceAmount = 0;
    uint16_t MegaTileID = 0;
};

struct StartLocationInfo
{
    int TileX = 0;
    int TileY = 0;
    int PlayerIndex = 0;
};

class GameMap
{
public:
    GameMap();

    void Generate(int Width, int Height);
    void SetTile(int X, int Y, const Tile& NewTile);
    const Tile& GetTile(int X, int Y) const;

    bool IsWalkable(int X, int Y) const;
    bool IsBuildable(int X, int Y) const;
    bool IsInBounds(int X, int Y) const;

    int GetWidth() const { return mWidth; }
    int GetHeight() const { return mHeight; }

    Core::Math::Vec2 TileToWorld(int X, int Y) const;
    void WorldToTile(const Core::Math::Vec2& WorldPos, int& OutX, int& OutY) const;

    // Load from CHK parser
    void LoadFromChk(const ChkParser& Parser);

    // Apply tileset terrain data (walkability/buildability from VF4/CV5)
    void ApplyTilesetData(const std::vector<uint16_t>* Cv5Data,
                          const std::vector<uint16_t>* Vf4Data);

    const std::vector<StartLocationInfo>& GetStartLocations() const { return mStartLocations; }
    std::vector<StartLocationInfo>& GetStartLocations() { return mStartLocations; }

    uint16_t GetMegaTile(int X, int Y) const;
    void SetMegaTile(int X, int Y, uint16_t MegaTileID);

private:
    int mWidth = DefaultMapWidth;
    int mHeight = DefaultMapHeight;
    std::vector<Tile> mTiles;
    std::vector<StartLocationInfo> mStartLocations;
};

} // namespace Game::Map
