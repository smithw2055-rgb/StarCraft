#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Game::Client
{

constexpr float TileSize = 32.0f;

enum class UnitSize : uint8_t
{
    Small,
    Medium,
    Large
};

enum class EntityKind : uint8_t
{
    Unit,
    Building,
    Resource,
    Effect
};

struct StartLocation
{
    int TileX = 0;
    int TileY = 0;
    int PlayerId = 0;
};

struct GameMap
{
    int Width = 0;
    int Height = 0;
    std::vector<uint16_t> MegaTileIds;   // 16-bit megatile index per tile
    std::vector<uint8_t> Walkable;       // 1 byte per tile
    std::vector<uint8_t> Creep;          // live gameplay creep mask per tile
    std::vector<StartLocation> StartLocations;
    std::string TilesetName;
};

} // namespace Game::Client
