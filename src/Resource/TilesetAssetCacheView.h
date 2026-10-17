#pragma once

#include <string>
#include <vector>

#include "Resource/TilesetAssetTypes.h"

namespace Resource::Graphics
{

class ResourceCache;

class TilesetAssetCacheView
{
public:
    explicit TilesetAssetCacheView(ResourceCache* Cache = nullptr)
        : mCache(Cache)
    {
    }

    void Reset(ResourceCache* Cache) { mCache = Cache; }
    bool HasCache() const { return mCache != nullptr; }

    bool SetTilesetPalette(const std::string& TilesetName) const;
    const std::vector<GpuImage>* GetTilesetImages(const std::string& TilesetName) const;
    const TilesetAtlas* GetTilesetAtlas(const std::string& TilesetName) const;
    const TilesetAtlas* GetTilesetAtlasForMap(const std::string& TilesetName, const std::vector<uint16_t>* MegaTileIds) const;
    const std::vector<TilesetCreepFrame>* GetTilesetCreepFrames(const std::string& TilesetName) const;
    const TilesetData* GetTilesetData(const std::string& TilesetName) const;

private:
    ResourceCache* mCache = nullptr;
};

} // namespace Resource::Graphics
