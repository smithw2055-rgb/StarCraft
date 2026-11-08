#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "Resource/UIAssetTypes.h"

namespace Resource { class GameDataLoader; }

namespace Resource::Graphics
{

class ResourceCache;

class ArchiveAssetSourceView
{
public:
    explicit ArchiveAssetSourceView(ResourceCache* Cache = nullptr)
        : mCache(Cache)
    {
    }

    bool HasCache() const { return mCache != nullptr; }

    std::optional<std::vector<uint8_t>> ReadFile(const std::string& Path) const;
    std::optional<std::vector<uint8_t>> ReadWithFallback(const std::string& Path) const;
    std::vector<std::string> DebugBuildExtractedAssetCandidates(const std::string& Path) const;

private:
    ResourceCache* mCache = nullptr;
};

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

class SpriteAssetCacheView
{
public:
    explicit SpriteAssetCacheView(ResourceCache* Cache = nullptr)
        : mCache(Cache)
    {
    }

    bool HasCache() const { return mCache != nullptr; }

    uint32_t ResolveImageID(int ChkUnitID) const;
    uint32_t ResolveConstructionImageID(int ChkUnitID) const;
    uint32_t ResolveSubunitImageID(int ChkUnitID) const;
    const std::vector<GpuImage>* GetSpriteFrames(uint32_t ImageID) const;
    const std::vector<GpuImage>* GetDatImageFrames(uint32_t ImageID) const;
    const std::vector<GpuImage>* GetSpriteOverlayFrames(uint32_t SpriteID) const;
    const std::vector<GpuImage>* GetSpriteFramesForChkUnit(int ChkUnitID) const;

private:
    ResourceCache* mCache = nullptr;
};

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
    bool EnsureWireframeFrame(int FrameIndex) const;
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
