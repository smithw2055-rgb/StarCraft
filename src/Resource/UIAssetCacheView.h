#pragma once

#include <string>
#include <vector>

#include "Resource/UIAssetTypes.h"

namespace Resource::Graphics
{

class ResourceCache;

class UIAssetCacheView
{
public:
    explicit UIAssetCacheView(ResourceCache* Cache = nullptr)
        : mCache(Cache)
    {
    }

    void Reset(ResourceCache* Cache) { mCache = Cache; }
    bool HasCache() const { return mCache != nullptr; }

    const std::vector<GpuImage>* GetUIIcons() const;
    const std::vector<std::string>* GetUIIconUris() const;
    const std::vector<GpuImage>* GetCommandIcons() const;
    const std::vector<GpuImage>* GetCommandIconMasks() const;
    const std::vector<std::string>* GetCommandIconUris() const;
    const std::vector<GpuImage>* GetCommandButtonSkins(CommandButtonSkinSet SkinSet) const;
    const std::vector<GpuImage>* GetWireframeFrames() const;
    const std::vector<GpuImage>* GetCursorFrames(const std::string& CursorName) const;
    const ImageOpaqueBounds* GetWireframeOpaqueBounds(int FrameIndex) const;
    const PortraitClip* GetPortraitClip(const std::string& PortraitKey) const;
    const PortraitFrame* GetPortraitFrame(const std::string& PortraitKey, double TimeSeconds) const;

    GpuImage GetHudAggIcons() const;
    GpuImage GetHudTwireStrip() const;
    GpuImage GetHudTunitStrip() const;
    GpuImage GetHudHpBarEmptyStrip() const;
    GpuImage GetHudHpBarStrip() const;

    GpuImage GetUIConsoleLeft() const;
    GpuImage GetUIConsoleCenter() const;
    GpuImage GetUIConsoleRight() const;
    GpuImage GetUIConsolePortraitFrame() const;
    GpuImage GetUIConsoleApm() const;
    GpuImage GetUIConsoleTime() const;
    GpuImage GetUIConsole() const;
    GpuImage GetUIConsoleOver() const;
    const PcxRegions* GetUIRegions() const;

private:
    ResourceCache* mCache = nullptr;
};

} // namespace Resource::Graphics
