#pragma once

#include <cstdint>
#include <array>
#include <string>
#include <unordered_map>
#include <vector>

#include "Resource/GpuImage.h"
#include "Resource/RawColor.h"

namespace Resource::Graphics
{

struct TilesetData
{
    std::vector<uint16_t> Vx4;        // 16 uint16 per megatile
    std::vector<uint16_t> Cv5;        // 16 uint16 per group
    std::vector<uint16_t> Vf4;        // 16 uint16 per megatile (walkability)
    bool Loaded = false;
};

struct TilesetAtlas
{
    GpuImage Image;
    int FrameWidth = 0;
    int FrameHeight = 0;
    int CellWidth = 0;
    int CellHeight = 0;
    int Columns = 0;
    int FrameCount = 0;
};

struct TilesetCreepFrame
{
    GpuImage Image;
    int OffsetX = 0;
    int OffsetY = 0;
    int WorldWidth = 0;
    int WorldHeight = 0;
};

struct TilesetAssetCacheState
{
    std::string ActiveName;
    std::unordered_map<uint8_t, std::array<RawColor, 256>> LightTableRgbaByColorShift;
    std::unordered_map<std::string, TilesetData> DataByName;
    std::unordered_map<std::string, std::vector<GpuImage>> ImagesByName;
    std::unordered_map<std::string, TilesetAtlas> AtlasesByName;
    std::unordered_map<std::string, std::vector<TilesetCreepFrame>> CreepFramesByName;
};

} // namespace Resource::Graphics
