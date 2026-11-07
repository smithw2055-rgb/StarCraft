#include "Resource/ResourceCache.h"

#include "Core/Utils/Logger.h"
#include "Resource/ArchiveAssetSourceView.h"
#include "Resource/DataAssetCacheView.h"
#include "Resource/SpriteAssetCacheView.h"
#include "Resource/TilesetAssetCacheView.h"
#include "Resource/UIAssetCacheView.h"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace Resource::Graphics
{

// ============================================================================
// Lifecycle & Cache Cleanup
// ============================================================================

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

// ============================================================================
// Sub-View Accessors
// ============================================================================

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

// ============================================================================
// Unit & Data Table Queries
// ============================================================================

std::optional<ColorPalette> ResourceCache::ReadUnitPalette()
{
    if (mSources.Archive)
    {
        auto D = mData.Loader.LoadUnitPalette(*mSources.Archive);
        ColorPalette Out = {};
        for (int I = 0; I < 256; ++I)
        {
            Out.Colors[I].R = D.Colors[I].R;
            Out.Colors[I].G = D.Colors[I].G;
            Out.Colors[I].B = D.Colors[I].B;
            Out.Colors[I].A = D.Colors[I].A;
        }
        return Out;
    }
    return std::nullopt;
}

uint32_t ResourceCache::ResolveImageID(int ChkUnitID)
{
    if (ChkUnitID < 0)
        return 0u;

    const uint32_t ImageID = mData.Loader.ResolveUnitImageID((uint32_t)ChkUnitID);
    if (ImageID != std::numeric_limits<uint32_t>::max())
        return ImageID;

    return (uint32_t)ChkUnitID;
}

uint32_t ResourceCache::ResolveConstructionImageID(int ChkUnitID)
{
    if (ChkUnitID < 0)
        return std::numeric_limits<uint32_t>::max();

    const uint32_t ImageID =
        mData.Loader.ResolveUnitConstructionImageID((uint32_t)ChkUnitID);
    if (ImageID != std::numeric_limits<uint32_t>::max())
        return ImageID;

    return std::numeric_limits<uint32_t>::max();
}

uint32_t ResourceCache::ResolveSubunitImageID(int ChkUnitID)
{
    if (ChkUnitID < 0)
        return std::numeric_limits<uint32_t>::max();
    return mData.Loader.ResolveUnitSubunitImageID((uint32_t)ChkUnitID);
}

// ============================================================================
// Tileset Metadata Queries
// ============================================================================

bool ResourceCache::SetTilesetPalette(const std::string& TilesetName)
{
    if (!mTilesets.ActiveName.empty() && mTilesets.ActiveName != TilesetName)
    {
        DestroySpriteCache();
        mTilesets.LightTableRgbaByColorShift.clear();
    }
    mTilesets.ActiveName = TilesetName;
    Core::Utils::Logger::Info("ResourceCache", "HD tilesets use RGBA megatile textures; skipping WPE palette load");
    return true;
}

TilesetData* ResourceCache::LoadTilesetData(const std::string& TilesetName)
{
    auto It = mTilesets.DataByName.find(TilesetName);
    if (It != mTilesets.DataByName.end()) return &It->second;

    auto TryRead = [&](const std::string& Base, bool Required) -> std::optional<std::vector<uint8_t>>
    {
        auto D = ReadFile(Base);
        if (D) return D;
        std::string Upper;
        Upper.reserve(Base.size());
        for (char C : Base) Upper += (char)std::toupper((unsigned char)C);
        auto D2 = ReadFile(Upper);
        if (D2) return D2;
        std::string Lower;
        Lower.reserve(Base.size());
        for (char C : Base) Lower += (char)std::tolower((unsigned char)C);
        auto D3 = ReadFile(Lower);
        if (D3) return D3;
        if (Required)
            Core::Utils::Logger::Warning("ResourceCache", "HD tileset metadata file not found: " + Base);
        return std::nullopt;
    };

    auto Vx4Data = TryRead("Tileset/" + TilesetName + ".vx4ex", true);
    if (!Vx4Data) return nullptr;
    auto Cv5Data = TryRead("Tileset/" + TilesetName + ".cv5", true);
    if (!Cv5Data) return nullptr;
    auto Vf4Data = TryRead("Tileset/" + TilesetName + ".vf4", true);
    if (!Vf4Data) return nullptr;

    TilesetData& Data = mTilesets.DataByName[TilesetName];
    auto Vx4Result = Vx4Decoder::Decode(*Vx4Data);
    Data.Vx4.clear();
    Data.Vx4.reserve(Vx4Result.MegaTileCount * 16);
    for (auto& M : Vx4Result.MegaTiles)
        for (int J = 0; J < 16; J++) Data.Vx4.push_back(M.MiniTileIDs[J]);

    auto Cv5Entries = Cv5Decoder::Decode(*Cv5Data);
    Data.Cv5.clear();
    Data.Cv5.reserve(Cv5Entries.size() * 16);
    for (auto& E : Cv5Entries)
        for (int J = 0; J < 16; J++) Data.Cv5.push_back(E.MegaTileIndices[J]);

    auto Vf4Result = Vf4Decoder::Decode(*Vf4Data);
    Data.Vf4.clear();
    Data.Vf4.reserve(Vf4Result.Entries.size() * 16);
    for (auto& E : Vf4Result.Entries)
        for (int J = 0; J < 16; J++) Data.Vf4.push_back(E.Flags[J]);

    Data.Loaded = true;
    Core::Utils::Logger::Info("ResourceCache",
        "Loaded HD tileset metadata " + TilesetName + ": VX4EX=" +
        std::to_string(Data.Vx4.size() / 16) + " entries, CV5=" +
        std::to_string(Data.Cv5.size() / 16) + " groups, VF4=" +
        std::to_string(Data.Vf4.size() / 16) + " entries");
    return &Data;
}

const TilesetData* ResourceCache::GetTilesetData(const std::string& TilesetName)
{
    auto It = mTilesets.DataByName.find(TilesetName);
    if (It != mTilesets.DataByName.end()) return &It->second;
    return LoadTilesetData(TilesetName);
}

// ============================================================================
// UI Asset Accessors
// ============================================================================

const std::vector<GpuImage>* ResourceCache::GetUIIcons()
{
    return mUI.Icons.empty() ? nullptr : &mUI.Icons;
}

const std::vector<std::string>* ResourceCache::GetUIIconUris()
{
    return mUI.IconUris.empty() ? nullptr : &mUI.IconUris;
}

const std::vector<GpuImage>* ResourceCache::GetCommandIcons()
{
    return mUI.CommandIcons.empty() ? nullptr : &mUI.CommandIcons;
}

const std::vector<GpuImage>* ResourceCache::GetCommandIconMasks()
{
    return mUI.CommandIconMasks.empty() ? nullptr : &mUI.CommandIconMasks;
}

const std::vector<std::string>* ResourceCache::GetCommandIconUris()
{
    return mUI.CommandIconUris.empty() ? nullptr : &mUI.CommandIconUris;
}

const std::vector<GpuImage>* ResourceCache::GetCommandButtonSkins(CommandButtonSkinSet SkinSet)
{
    const auto& Frames = mUI.CommandButtonSkins[static_cast<size_t>(SkinSet)];
    return Frames.empty() ? nullptr : &Frames;
}

const std::vector<GpuImage>* ResourceCache::GetWireframeFrames()
{
    return mUI.WireframeFrames.empty() ? nullptr : &mUI.WireframeFrames;
}

const ImageOpaqueBounds* ResourceCache::GetWireframeOpaqueBounds(int FrameIndex) const
{
    if (FrameIndex < 0 || FrameIndex >= (int)mUI.WireframeOpaqueBounds.size())
        return nullptr;
    const_cast<ResourceCache*>(this)->EnsureWireframeFrame(FrameIndex);
    return &mUI.WireframeOpaqueBounds[(size_t)FrameIndex];
}

const std::vector<GpuImage>* ResourceCache::GetCursorFrames(const std::string& CursorName)
{
    auto It = mUI.CursorFrames.find(CursorName);
    return It != mUI.CursorFrames.end() ? &It->second : nullptr;
}

GpuImage ResourceCache::GetHudAggIcons() { return mUI.HudAggIcons; }
GpuImage ResourceCache::GetHudTwireStrip() { return mUI.HudTwireStrip; }
GpuImage ResourceCache::GetHudTunitStrip() { return mUI.HudTunitStrip; }
GpuImage ResourceCache::GetHudHpBarEmptyStrip() { return mUI.HudHpBarEmptyStrip; }
GpuImage ResourceCache::GetHudHpBarStrip() { return mUI.HudHpBarStrip; }
GpuImage ResourceCache::GetUIConsoleLeft() { return mUI.ConsoleLeft; }
GpuImage ResourceCache::GetUIConsoleCenter() { return mUI.ConsoleCenter; }
GpuImage ResourceCache::GetUIConsoleRight() { return mUI.ConsoleRight; }
GpuImage ResourceCache::GetUIConsolePortraitFrame() { return mUI.ConsolePortraitFrame; }
GpuImage ResourceCache::GetUIConsoleApm() { return mUI.ConsoleApm; }
GpuImage ResourceCache::GetUIConsoleTime() { return mUI.ConsoleTime; }
GpuImage ResourceCache::GetUIConsole() { return mUI.Console; }
GpuImage ResourceCache::GetUIConsoleOver() { return mUI.ConsoleOver; }

} // namespace Resource::Graphics
