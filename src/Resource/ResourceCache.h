#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "Resource/Cv5Decoder.h"
#include "Resource/Vx4Decoder.h"
#include "Resource/Vf4Decoder.h"
#include "Resource/PcxRegion.h"
#include "Resource/GrpToGpu.h"
#include "Resource/ArchiveSourceTypes.h"
#include "Resource/DataAssetTypes.h"
#include "Resource/UIAssetTypes.h"
#include "Resource/SpriteAssetTypes.h"
#include "Resource/TilesetAssetTypes.h"

namespace Resource::Graphics
{

class ArchiveAssetSourceView;
class DataAssetCacheView;
class SpriteAssetCacheView;
class TilesetAssetCacheView;
class UIAssetCacheView;



class ResourceCache
{
public:
    ResourceCache();
    ~ResourceCache();

    // Open local extracted assets under GameInstallPath/assets. Returns true on success.
    bool Initialize(const std::string& GameInstallPath);
    void Shutdown();

    bool IsInitialized() const { return mIsInitialized; }

    ArchiveAssetSourceView GetArchiveSource();
    DataAssetCacheView GetDataAssets();
    SpriteAssetCacheView GetSpriteAssets();
    TilesetAssetCacheView GetTilesetAssets();
    UIAssetCacheView GetUIAssets();
    // Data table access
    Resource::GameDataLoader& GetDataLoader() { return mData.Loader; }
    const Resource::GameDataLoader& GetDataLoader() const { return mData.Loader; }

    // Sprite lookup by CHK unit ID. Returns array of GPU images (one per frame).
    // Cached internally. Returns nullptr if the sprite could not be loaded.
    const std::vector<GpuImage>* GetSpriteFrames(uint32_t ImageID);
    const std::vector<GpuImage>* GetDatImageFrames(uint32_t ImageID);
    const std::vector<GpuImage>* GetSpriteOverlayFrames(uint32_t SpriteID);

    // Current active palette for non-HD indexed assets.
    const ColorPalette& GetPalette() const { return mData.ActivePalette; }

    // Prepare the active HD tileset. HD terrain uses RGBA DDS megatiles and does not load WPE.
    bool SetTilesetPalette(const std::string& TilesetName);
    const TilesetData* GetTilesetData(const std::string& TilesetName);
    TilesetData* LoadTilesetData(const std::string& TilesetName);

    // Tileset megatile textures: returns array of GPU images (one per megatile index)
    const std::vector<GpuImage>* GetTilesetImages(const std::string& TilesetName);
    const TilesetAtlas* GetTilesetAtlas(const std::string& TilesetName);

    // Direct archive read (for raw files like scenario.chk)
    std::optional<std::vector<uint8_t>> ReadFile(const std::string& Path);
    std::vector<std::string> DebugBuildExtractedAssetCandidates(const std::string& Path) const;

    // UI graphics
    const std::vector<GpuImage>* GetUIIcons();
    const std::vector<std::string>* GetUIIconUris();
    const std::vector<GpuImage>* GetCommandIcons();
    const std::vector<GpuImage>* GetCommandIconMasks();
    const std::vector<std::string>* GetCommandIconUris();
    const std::vector<GpuImage>* GetCommandButtonSkins(CommandButtonSkinSet SkinSet);
    const std::vector<GpuImage>* GetWireframeFrames();
    const ImageOpaqueBounds* GetWireframeOpaqueBounds(int FrameIndex) const;
    const std::vector<GpuImage>* GetCursorFrames(const std::string& CursorName);
    const PortraitClip* GetPortraitClip(const std::string& PortraitKey);
    const PortraitFrame* GetPortraitFrame(const std::string& PortraitKey, double TimeSeconds);
    GpuImage GetHudAggIcons();
    GpuImage GetHudTwireStrip();
    GpuImage GetHudTunitStrip();
    GpuImage GetHudHpBarEmptyStrip();
    GpuImage GetHudHpBarStrip();
    GpuImage GetUIConsoleLeft();
    GpuImage GetUIConsoleCenter();
    GpuImage GetUIConsoleRight();
    GpuImage GetUIConsolePortraitFrame();
    GpuImage GetUIConsoleApm();
    GpuImage GetUIConsoleTime();
    GpuImage GetUIConsole();
    GpuImage GetUIConsoleOver();
    const PcxRegions& GetUIRegions() const { return mUI.Regions; }

    // Helpers
    bool OpenMpqArchives(const std::string& InstallPath);
    bool OpenCascStorage(const std::string& StoragePath);
    bool OpenAssetsDirectory(const std::string& AssetsPath);

    // Entity sprite keys now use CHK unit IDs directly.
    uint32_t ResolveImageID(int ChkUnitID);
    uint32_t ResolveConstructionImageID(int ChkUnitID);
    uint32_t ResolveSubunitImageID(int ChkUnitID);

    // Read .pal via GameDataLoader convenience
    std::optional<ColorPalette> ReadUnitPalette();

    // Read raw file with case-insensitive variants
    std::optional<std::vector<uint8_t>> ReadWithFallback(const std::string& Path);

private:
    bool LoadUIGraphics();
    std::optional<std::vector<uint8_t>> ReadUIFileWithUpperFallback(const std::string& Path);
    std::optional<std::filesystem::path> FindExtractedUIAssetPath(const std::string& LogicalPath) const;
    bool LoadWicUIImage(const std::string& LogicalPath, GpuImage& Out, bool TransparentBlack = false);
    bool LoadNamedConsoleDds(const std::string& FileName, GpuImage& Out, bool TransparentBlack = false);
    bool BuildConsoleCompositeFromDds(GpuImage& Out);
    bool LoadHdConsoleGraphics();
    bool LoadClassicConsoleGraphics();
    bool UploadDecodedUIFrames(const DecodedSprite& Sprite,
                               std::vector<GpuImage>& Out,
                               std::vector<ImageOpaqueBounds>* OutBounds = nullptr,
                               bool AsMask = false);
    bool LoadUIGrpFrames(const std::string& Path,
                         std::vector<GpuImage>& Out,
                         const GrpDecodeOptions& Options = {},
                         std::vector<ImageOpaqueBounds>* OutBounds = nullptr,
                         bool AsMask = false);
    bool LoadDdsGrpUIFrames(const std::string& Path,
                            std::vector<GpuImage>& Out,
                            std::vector<ImageOpaqueBounds>* OutBounds = nullptr,
                            bool AsMask = false);
    std::string GetDdsGrpLogicalPath(const std::string& ClassicPath) const;
    bool LoadPreferredUIFrames(const std::string& ClassicPath,
                               std::vector<GpuImage>& Out,
                               const GrpDecodeOptions& Options = {},
                               std::vector<ImageOpaqueBounds>* OutBounds = nullptr,
                               bool AsMask = false,
                               const char* DebugLabel = nullptr);
    bool ExportPreferredUIFramesForGui(const std::string& ClassicPath,
                                       std::vector<std::string>& OutUris,
                                       const GrpDecodeOptions& Options = {},
                                       bool AsMask = false,
                                       const char* DebugLabel = nullptr);
    bool LoadCommandButtonGraphics(const GrpDecodeOptions& CommandButtonOptions);
    bool LoadDdsGrpUIFirstFrame(const std::string& Path, GpuImage& Out);
    bool LoadPcxUIImage(const std::string& Path, GpuImage& Out, bool TransparentBlack = false);
    bool LoadUIIconGraphics();
    bool LoadWireframeGraphics();
    bool LoadHudChromeGraphics();
    bool LoadBlinkGraphics();
    bool LoadCursorGraphics(const GrpDecodeOptions& CursorOptions);
    bool BuildTilesetImages(const std::string& TilesetName);
    bool BuildTilesetAtlas(const std::string& TilesetName);
    const std::vector<GpuImage>* LoadSpriteFramesForPath(uint32_t CacheKey,
                                                         const std::string& GrpPath,
                                                         const char* DebugLabel = nullptr,
                                                         const GrpDecodeOptions& Options = {});
    std::optional<std::vector<uint8_t>> ReadExtractedAsset(const std::string& Path);
    std::vector<std::string> BuildExtractedAssetCandidates(const std::string& Path) const;
    bool LoadPortraitClip(const std::string& PortraitKey, PortraitClip& OutClip);
    void DestroySpriteCache();
    void DestroyTilesetCache();
    void DestroyUICache();

    ArchiveSourceState mSources;

    DataAssetCacheState mData;

    bool mIsInitialized = false;

    // Sprite cache: ImageID -> frames
    SpriteAssetCacheState mSprites;

    // Tileset cache: name -> data + images
    TilesetAssetCacheState mTilesets;

    // UI cache
    UIAssetCacheState mUI;

    // CASC path resolution
    std::string ResolveCascPath(const std::string& Path);
};

} // namespace Resource::Graphics








