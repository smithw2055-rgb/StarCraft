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

struct TilesetAssetCacheState
{
    std::unordered_map<std::string, TilesetData> DataByName;
    std::unordered_map<std::string, std::vector<GpuImage>> ImagesByName;
};

} // namespace Resource::Graphics
