#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "Resource/GpuImage.h"

namespace Resource::Graphics
{

struct SpriteAssetCacheState
{
    std::unordered_map<uint32_t, std::vector<GpuImage>> FramesByImageId;
};

} // namespace Resource::Graphics
