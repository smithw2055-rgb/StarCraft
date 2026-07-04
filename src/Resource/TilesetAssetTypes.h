#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "Resource/GpuImage.h"

namespace Resource::Graphics
{

struct TilesetData
{
    std::vector<uint8_t> Vr4;
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

struct TilesetAssetCacheState
{
    std::unordered_map<std::string, TilesetData> DataByName;
    std::unordered_map<std::string, std::vector<GpuImage>> ImagesByName;
    std::unordered_map<std::string, TilesetAtlas> AtlasesByName;
};

} // namespace Resource::Graphics
