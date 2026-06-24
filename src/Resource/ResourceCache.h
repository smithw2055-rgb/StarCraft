#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <optional>

#include "raylib.h"
#include "Resource/IArchiveReader.h"
#include "Resource/GameDataLoader.h"
#include "Resource/GrpDecoder.h"
#include "Resource/PalDecoder.h"
#include "Resource/PcxDecoder.h"
#include "Resource/GameDetector.h"

namespace Resource
{

class ResourceCache
{
public:
    ResourceCache();
    ~ResourceCache();

    bool Initialize(const std::string& GameInstallPath);

    // Texture lookup by image/sprite ID
    const std::vector<Texture2D>* GetSpriteTextures(uint32_t ImageID);

    // Load UI graphics from game\ directory
    bool LoadUIGraphics();
    const std::vector<Texture2D>* GetUIIcons() const { return mUIIcons.empty() ? nullptr : &mUIIcons; }
    Texture2D GetUIBlink() const { return mUIBlink; }
    Texture2D GetUIConsole() const { return mUIConsole; }
    Texture2D GetUIConsoleOver() const { return mUIConsoleOver; }
    const PcxRegions& GetUIRegions() const { return mUIRegions; }

    // Direct file read through archive
    std::optional<std::vector<uint8_t>> ReadFile(const std::string& Path);

    // Set tileset-specific palette (from WPE file)
    bool SetTilesetPalette(const std::string& TilesetName);

    // Load and cache tileset megatile textures (from VR4+VX4)
    const std::vector<Texture2D>* GetTilesetTextures(const std::string& TilesetName);

    // Tileset raw data access
    struct TilesetData
    {
        std::vector<uint8_t> Vr4;
        std::vector<uint16_t> Vx4;
        std::vector<uint16_t> Cv5;
        std::vector<uint16_t> Vf4;     // 16 uint16 per megatile (walkability/height flags)
        bool Loaded = false;
    };
    TilesetData* GetTilesetData(const std::string& TilesetName);

    // Accessors
    GameDataLoader& GetDataLoader() { return mDataLoader; }
    IArchiveReader& GetArchive() { return *mArchive; }

    bool IsInitialized() const { return mIsInitialized; }

private:
    bool LoadSpriteArchive();

    std::unique_ptr<IArchiveReader> mArchive;
    std::unique_ptr<IArchiveReader> mArchive2;
    std::unique_ptr<IArchiveReader> mArchive3; // Patch_rt.mpq (highest priority)
    GameDataLoader mDataLoader;
    RawPalette mPalette;

    bool mIsInitialized = false;

    std::unordered_map<uint32_t, std::vector<Texture2D>> mTextureCache;

    std::unordered_map<std::string, TilesetData> mTilesetData;
    std::unordered_map<std::string, std::vector<Texture2D>> mTilesetCache;

    std::vector<Texture2D> mUIIcons;
    Texture2D mUIBlink = {};
    Texture2D mUIConsole = {};
    Texture2D mUIConsoleOver = {};
    PcxRegions mUIRegions;

    TilesetData* LoadTilesetData(const std::string& TilesetName);

    // Helper: open CASC storage
    bool OpenCascStorage(const std::string& InstallPath,
                         std::unique_ptr<IArchiveReader>& OutArchive);

    // Helper: open MPQ with priority fallback
    bool OpenMpqArchives(const std::string& InstallPath);

    // Helper: open assets directory (extracted resources)
    bool OpenAssetsDirectory(const std::string& AssetsPath);

    bool mIsCasc = false;
    bool mUseAssets = false;  // Use extracted assets instead of archives
};

} // namespace Resource
