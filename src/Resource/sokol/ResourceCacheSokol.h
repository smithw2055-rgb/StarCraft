#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "third_party/sokol/sokol_gfx.h"

#include "Resource/GameDataLoader.h"
#include "Resource/Cv5Decoder.h"
#include "Resource/Vx4Decoder.h"
#include "Resource/Vf4Decoder.h"
#include "Resource/PcxRegion.h"
#include "Resource/IArchiveReader.h"

#include "Resource/sokol/GrpToSokol.h"
#include "Resource/sokol/PaletteSokol.h"

namespace Resource::SokolBridge
{

enum class CommandButtonSkinSet : uint8_t
{
    Terran = 0,
    Protoss,
    Zerg,
    Count
};

struct ImageOpaqueBounds
{
    int X = 0;
    int Y = 0;
    int W = 0;
    int H = 0;
    bool Valid() const { return W > 0 && H > 0; }
};

class ResourceCacheSokol
{
public:
    struct PortraitFrame
    {
        SokolImage Image = {};
        float DurationSeconds = 1.0f / 12.0f;
    };

    struct PortraitClip
    {
        int Width = 0;
        int Height = 0;
        std::vector<PortraitFrame> Frames;
        bool Valid() const { return !Frames.empty(); }
    };

    ResourceCacheSokol();
    ~ResourceCacheSokol();

    // Open game data at InstallPath. Returns true on success.
    bool Initialize(const std::string& GameInstallPath);
    void Shutdown();

    bool IsInitialized() const { return mIsInitialized; }

    // Data table access
    GameDataLoader& GetDataLoader() { return mDataLoader; }
    const GameDataLoader& GetDataLoader() const { return mDataLoader; }

    // Sprite lookup by CHK unit ID. Returns array of sokol images (one per frame).
    // Cached internally. Returns nullptr if the sprite could not be loaded.
    const std::vector<SokolImage>* GetSpriteFrames(uint32_t ImageID);

    // Current active palette (set by SetTilesetPalette or unit palette)
    const SokoPalette& GetPalette() const { return mActivePalette; }

    // Set the active tileset (loads .wpe into palette, invalidates tileset cache)
    bool SetTilesetPalette(const std::string& TilesetName);

    // Tileset raw data
    struct TilesetData
    {
        std::vector<uint8_t> Vr4;
        std::vector<uint16_t> Vx4;        // 16 uint16 per megatile
        std::vector<uint16_t> Cv5;        // 16 uint16 per group
        std::vector<uint16_t> Vf4;        // 16 uint16 per megatile (walkability)
        bool Loaded = false;
    };
    const TilesetData* GetTilesetData(const std::string& TilesetName);
    TilesetData* LoadTilesetData(const std::string& TilesetName);

    // Tileset megatile textures: returns array of sokol images (one per megatile index)
    const std::vector<SokolImage>* GetTilesetImages(const std::string& TilesetName);

    // Direct archive read (for raw files like scenario.chk)
    std::optional<std::vector<uint8_t>> ReadFile(const std::string& Path);
    std::vector<std::string> DebugBuildExtractedAssetCandidates(const std::string& Path) const;

    // UI graphics
    const std::vector<SokolImage>* GetUIIcons();
    const std::vector<SokolImage>* GetCommandIcons();
    const std::vector<SokolImage>* GetCommandIconMasks();
    const std::vector<SokolImage>* GetCommandButtonSkins(CommandButtonSkinSet SkinSet);
    const std::vector<SokolImage>* GetWireframeFrames();
    const ImageOpaqueBounds* GetWireframeOpaqueBounds(int FrameIndex) const;
    const std::vector<SokolImage>* GetCursorFrames(const std::string& CursorName);
    const PortraitClip* GetPortraitClip(const std::string& PortraitKey);
    SokolImage GetHudAggIcons();
    SokolImage GetHudTwireStrip();
    SokolImage GetHudTunitStrip();
    SokolImage GetHudHpBarEmptyStrip();
    SokolImage GetHudHpBarStrip();
    SokolImage GetUIConsoleLeft();
    SokolImage GetUIConsoleCenter();
    SokolImage GetUIConsoleRight();
    SokolImage GetUIConsolePortraitFrame();
    SokolImage GetUIConsoleApm();
    SokolImage GetUIConsoleTime();
    SokolImage GetUIConsole();
    SokolImage GetUIConsoleOver();
    const PcxRegions& GetUIRegions() const { return mUIRegions; }

    // Helpers
    bool OpenMpqArchives(const std::string& InstallPath);
    bool OpenCascStorage(const std::string& StoragePath);
    bool OpenAssetsDirectory(const std::string& AssetsPath);

    // Entity sprite keys now use CHK unit IDs directly.
    uint32_t ResolveImageID(int ChkUnitID);

    // Read .pal via GameDataLoader convenience
    std::optional<SokoPalette> ReadUnitPalette();

    // Read raw file with case-insensitive variants
    std::optional<std::vector<uint8_t>> ReadWithFallback(const std::string& Path);

private:
    bool LoadUIGraphics();
    bool BuildTilesetImages(const std::string& TilesetName);
    std::optional<std::vector<uint8_t>> ReadExtractedAsset(const std::string& Path);
    std::vector<std::string> BuildExtractedAssetCandidates(const std::string& Path) const;
    bool LoadPortraitClip(const std::string& PortraitKey, PortraitClip& OutClip);
    void DestroySpriteCache();
    void DestroyTilesetCache();
    void DestroyUICache();

    std::unique_ptr<IArchiveReader> mArchive;
    std::unique_ptr<IArchiveReader> mArchive2;
    std::unique_ptr<IArchiveReader> mArchive3;  // patch_rt (highest priority)
    std::string mListfilePath;

    GameDataLoader mDataLoader;
    SokoPalette mActivePalette;

    bool mIsInitialized = false;
    bool mIsCasc = false;

    // Sprite cache: ImageID → frames
    bool mUseExtractedAssets = false;
    std::string mAssetsRoot;
    std::unordered_map<uint32_t, std::vector<SokolImage>> mSpriteCache;

    // Tileset cache: name → data + images
    std::unordered_map<std::string, TilesetData> mTilesetData;
    std::unordered_map<std::string, std::vector<SokolImage>> mTilesetImages;

    // UI cache
    std::vector<SokolImage> mUIIcons;
    std::vector<SokolImage> mCommandIcons;
    std::vector<SokolImage> mCommandIconMasks;
    std::array<std::vector<SokolImage>, static_cast<size_t>(CommandButtonSkinSet::Count)> mCommandButtonSkins;
    std::vector<SokolImage> mWireframeFrames;
    std::vector<ImageOpaqueBounds> mWireframeOpaqueBounds;
    std::unordered_map<std::string, std::vector<SokolImage>> mCursorFrames;
    std::unordered_map<std::string, PortraitClip> mPortraitClips;
    SokolImage mHudAggIcons = {};
    SokolImage mHudTwireStrip = {};
    SokolImage mHudTunitStrip = {};
    SokolImage mHudHpBarEmptyStrip = {};
    SokolImage mHudHpBarStrip = {};
    SokolImage mUIConsoleLeft = {};
    SokolImage mUIConsoleCenter = {};
    SokolImage mUIConsoleRight = {};
    SokolImage mUIConsolePortraitFrame = {};
    SokolImage mUIConsoleApm = {};
    SokolImage mUIConsoleTime = {};
    SokolImage mUIBlink = {};
    SokolImage mUIConsole = {};
    SokolImage mUIConsoleOver = {};
    PcxRegions mUIRegions;

    // CASC path resolution
    std::string ResolveCascPath(const std::string& Path);
    std::vector<std::string> mCascPrefixes = {"", "base1/", "sc1/", "sc1bw/"};
    std::unordered_map<std::string, std::string> mCascPathCache;
    bool mMediaFoundationInitialized = false;
};

} // namespace Resource::SokolBridge
