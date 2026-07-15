#include "Resource/ResourceCache.h"

namespace Resource::Graphics
{

ResourceCache::ResourceCache() = default;

ResourceCache::~ResourceCache()
{
    Shutdown();
}

void ResourceCache::DestroySpriteCache()
{
    for (auto& [ID, Frames] : mSprites.FramesByImageId)
        for (auto& F : Frames) DestroyImage(F);
    mSprites.FramesByImageId.clear();
}

void ResourceCache::DestroyTilesetCache()
{
    for (auto& [Name, Frames] : mTilesets.ImagesByName)
        for (auto& F : Frames) DestroyImage(F);
    mTilesets.ImagesByName.clear();
    for (auto& [Name, Atlas] : mTilesets.AtlasesByName)
        DestroyImage(Atlas.Image);
    mTilesets.AtlasesByName.clear();
    for (auto& [Name, Frames] : mTilesets.CreepFramesByName)
        for (auto& Frame : Frames) DestroyImage(Frame.Image);
    mTilesets.CreepFramesByName.clear();
    mTilesets.DataByName.clear();
    mTilesets.LightTableRgbaByColorShift.clear();
    mTilesets.ActiveName.clear();
}

void ResourceCache::DestroyUICache()
{
    for (auto& I : mUI.Icons) DestroyImage(I);
    mUI.Icons.clear();
    for (auto& I : mUI.CommandIcons) DestroyImage(I);
    mUI.CommandIcons.clear();
    for (auto& I : mUI.CommandIconMasks) DestroyImage(I);
    mUI.CommandIconMasks.clear();
    for (auto& Frames : mUI.CommandButtonSkins)
    {
        for (auto& I : Frames)
            DestroyImage(I);
        Frames.clear();
    }
    for (auto& I : mUI.WireframeFrames) DestroyImage(I);
    mUI.WireframeFrames.clear();
    mUI.WireframeOpaqueBounds.clear();
    for (auto& [Name, Frames] : mUI.CursorFrames)
        for (auto& Frame : Frames) DestroyImage(Frame);
    mUI.CursorFrames.clear();
    for (auto& [Name, Clip] : mUI.PortraitClips)
    {
        for (auto& Frame : Clip.Frames)
            DestroyImage(Frame.Image);
        DestroyImage(Clip.CurrentFrame.Image);
    }
    mUI.PortraitClips.clear();
    DestroyImage(mUI.HudAggIcons);
    DestroyImage(mUI.HudTwireStrip);
    DestroyImage(mUI.HudTunitStrip);
    DestroyImage(mUI.HudHpBarEmptyStrip);
    DestroyImage(mUI.HudHpBarStrip);
    DestroyImage(mUI.ConsoleLeft);
    DestroyImage(mUI.ConsoleCenter);
    DestroyImage(mUI.ConsoleRight);
    DestroyImage(mUI.ConsolePortraitFrame);
    DestroyImage(mUI.ConsoleApm);
    DestroyImage(mUI.ConsoleTime);
    DestroyImage(mUI.Blink);
    DestroyImage(mUI.Console);
    DestroyImage(mUI.ConsoleOver);
}

void ResourceCache::Shutdown()
{
    DestroySpriteCache();
    DestroyTilesetCache();
    DestroyUICache();
    mSources.Archive.reset();
    mSources.Archive2.reset();
    mSources.Archive3.reset();
    mSources.AssetsRoot.clear();
    mSources.UseExtractedAssets = false;
    mSources.IsCasc = false;
    mIsInitialized = false;
}

} // namespace Resource::Graphics
