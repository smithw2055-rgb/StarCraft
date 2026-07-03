#pragma once

namespace Resource { class GameDataLoader; }

namespace Resource::Graphics
{

class ResourceCache;

class DataAssetCacheView
{
public:
    explicit DataAssetCacheView(ResourceCache* Cache = nullptr)
        : mCache(Cache)
    {
    }

    bool HasCache() const { return mCache != nullptr; }
    Resource::GameDataLoader* GetDataLoader() const;

private:
    ResourceCache* mCache = nullptr;
};

} // namespace Resource::Graphics
