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

// === Consolidated from src/Resource/TilesetAssetCacheView.h ===
#include <string>
#include <vector>


#include <cstdint>
#include <array>
#include <string>
#include <unordered_map>
#include <vector>

#include "Resource/UIAssetTypes.h"
#include "Resource/RawColor.h"

namespace Resource::Graphics
{

struct TilesetData
{
    std::vector<uint16_t> Vx4;        // 16 uint16 per megatile
    std::vector<uint16_t> Cv5;        // 16 uint16 per group
    std::vector<uint16_t> Vf4;        // 16 uint16 per megatile (walkability)
    bool Loaded = false;
};

struct TilesetAtlas
{
    GpuImage Image;
    int FrameWidth = 0;
    int FrameHeight = 0;
    int CellWidth = 0;
    int CellHeight = 0;
    int Columns = 0;
    int FrameCount = 0;
    std::vector<int> MegaIdxToSlot;
};

struct TilesetCreepFrame
{
    GpuImage Image;
    int OffsetX = 0;
    int OffsetY = 0;
    int WorldWidth = 0;
    int WorldHeight = 0;
};

struct TilesetAssetCacheState
{
    std::string ActiveName;
    std::unordered_map<uint8_t, std::array<RawColor, 256>> LightTableRgbaByColorShift;
    std::unordered_map<std::string, TilesetData> DataByName;
    std::unordered_map<std::string, std::vector<GpuImage>> ImagesByName;
    std::unordered_map<std::string, TilesetAtlas> AtlasesByName;
    std::unordered_map<std::string, std::vector<TilesetCreepFrame>> CreepFramesByName;
};

} // namespace Resource::Graphics

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
