#pragma once

#include "Game/sokol/Entity.h"
#include "Resource/sokol/ResourceCacheSokol.h"
#include "Render/sokol/Camera2D.h"

namespace Game::SokolDemo
{

class TilesetRenderer
{
public:
    // Tileset name and the ResourceCache to draw from.
    void Init(const std::string& TilesetName, Resource::SokolBridge::ResourceCacheSokol& Cache);

    // Set the active camera so viewport culling works.
    void SetCamera(const Render::Camera2D& Camera) { mCamera = &Camera; }

    // Render visible tiles of the map.
    void Render(const GameMap& Map);

    // True if the real megatile atlas is loaded; false means synthetic fallback.
    bool HasRealAtlas() const { return mMegatileCount > 0; }

    // Look up the megatile image for a given raw TileID (already remapped through CV5)
    const Resource::SokolBridge::SokolImage* GetMegatileImage(uint16_t MegatileIdx) const;

    const std::string& GetTilesetName() const { return mTilesetName; }

    // Number of megatiles in the current tileset
    int GetMegatileCount() const { return mMegatileCount; }

private:
    std::string mTilesetName;
    Resource::SokolBridge::ResourceCacheSokol* mCache = nullptr;
    const Render::Camera2D* mCamera = nullptr;
    int mMegatileCount = 0;

    // CV5 lookup: group index → megatile index (16 entries per group)
    const std::vector<uint16_t>* mCv5 = nullptr;
};

} // namespace Game::SokolDemo
