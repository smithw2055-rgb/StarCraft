#pragma once

#include <cstdint>
#include <vector>

#include "Resource/GpuImage.h"

namespace Resource::Graphics
{

class ResourceCache;

class SpriteAssetCacheView
{
public:
    explicit SpriteAssetCacheView(ResourceCache* Cache = nullptr)
        : mCache(Cache)
    {
    }

    bool HasCache() const { return mCache != nullptr; }

    uint32_t ResolveImageID(int ChkUnitID) const;
    uint32_t ResolveSubunitImageID(int ChkUnitID) const;
    const std::vector<GpuImage>* GetSpriteFrames(uint32_t ImageID) const;
    const std::vector<GpuImage>* GetDatImageFrames(uint32_t ImageID) const;
    const std::vector<GpuImage>* GetSpriteOverlayFrames(uint32_t SpriteID) const;
    const std::vector<GpuImage>* GetSpriteFramesForChkUnit(int ChkUnitID) const;

private:
    ResourceCache* mCache = nullptr;
};

} // namespace Resource::Graphics
