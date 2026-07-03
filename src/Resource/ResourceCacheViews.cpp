#include "Resource/ResourceCache.h"

#include "Resource/ArchiveAssetSourceView.h"
#include "Resource/DataAssetCacheView.h"
#include "Resource/SpriteAssetCacheView.h"
#include "Resource/TilesetAssetCacheView.h"
#include "Resource/UIAssetCacheView.h"

namespace Resource::Graphics
{

ArchiveAssetSourceView ResourceCache::GetArchiveSource()
{
    return ArchiveAssetSourceView(this);
}

DataAssetCacheView ResourceCache::GetDataAssets()
{
    return DataAssetCacheView(this);
}

SpriteAssetCacheView ResourceCache::GetSpriteAssets()
{
    return SpriteAssetCacheView(this);
}

TilesetAssetCacheView ResourceCache::GetTilesetAssets()
{
    return TilesetAssetCacheView(this);
}

UIAssetCacheView ResourceCache::GetUIAssets()
{
    return UIAssetCacheView(this);
}

} // namespace Resource::Graphics