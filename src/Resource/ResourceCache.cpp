#include "Resource/ResourceCache.h"

#include "Core/Utils/Logger.h"
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


// === Consolidated from src/Resource/ResourceCacheInitialize.cpp ===


#include <algorithm>
#include <filesystem>
#include <unordered_set>

#ifdef _WIN32
#include <windows.h>
#endif

namespace Resource::Graphics
{

namespace
{
    std::string NormalizeLogicalPath(const std::string& Path)
    {
        std::string Normalized = Path;
        for (char& C : Normalized)
        {
            if (C == '\\')
                C = '/';
        }
        return Normalized;
    }

    std::string ToLowerCopy(const std::string& Value)
    {
        std::string Lower = Value;
        std::transform(Lower.begin(), Lower.end(), Lower.begin(),
            [](unsigned char C) { return (char)std::tolower(C); });
        return Lower;
    }

    void AddUniquePath(std::vector<std::string>& Paths,
                       std::unordered_set<std::string>& Seen,
                       const std::string& Candidate)
    {
        if (Candidate.empty())
            return;

        std::string Normalized = NormalizeLogicalPath(Candidate);
        std::string Key = ToLowerCopy(Normalized);
        if (Seen.insert(Key).second)
            Paths.push_back(Normalized);
    }

    std::string GetExecutableDir()
    {
#ifdef _WIN32
        char Buffer[MAX_PATH] = {};
        DWORD Length = GetModuleFileNameA(nullptr, Buffer, MAX_PATH);
        if (Length == 0 || Length >= MAX_PATH)
            return {};
        return std::filesystem::path(Buffer).parent_path().string();
#else
        return {};
#endif
    }

}

bool ResourceCache::Initialize(const std::string& GameInstallPath)
{
    Core::Utils::Logger::Info(
        "ResourceCache",
        "Initializing this=" + std::to_string((uintptr_t)this) + " with: " + GameInstallPath);

    mIsInitialized = false;
    mSources.IsCasc = false;
    mSources.UseExtractedAssets = false;
    mSources.AssetsRoot.clear();

    bool UsingExtractedAssets = false;
    std::vector<std::string> AssetCandidates;
    std::unordered_set<std::string> SeenAssets;
    AddUniquePath(AssetCandidates, SeenAssets, GameInstallPath + "/assets");
    AddUniquePath(AssetCandidates, SeenAssets, "./assets");

    const std::string ExeDir = GetExecutableDir();
    if (!ExeDir.empty())
    {
        std::filesystem::path Base(ExeDir);
        AddUniquePath(AssetCandidates, SeenAssets, (Base / "assets").string());
        AddUniquePath(AssetCandidates, SeenAssets, (Base / "../assets").lexically_normal().string());
        AddUniquePath(AssetCandidates, SeenAssets, (Base / "../../assets").lexically_normal().string());
        AddUniquePath(AssetCandidates, SeenAssets, (Base / "../../../assets").lexically_normal().string());
    }

    for (const auto& AssetsPath : AssetCandidates)
    {
        Core::Utils::Logger::Info("ResourceCache", "Probe assets candidate: " + AssetsPath);
        std::filesystem::path CandidatePath(AssetsPath);
        if (!std::filesystem::exists(CandidatePath) || !std::filesystem::is_directory(CandidatePath))
            continue;

        if (OpenAssetsDirectory(AssetsPath))
        {
            UsingExtractedAssets = true;
            Core::Utils::Logger::Info("ResourceCache",
                "Using extracted assets from: " + AssetsPath);
            break;
        }
    }

    if (!UsingExtractedAssets)
    {
        Core::Utils::Logger::Warning("ResourceCache",
            "Local assets directory not found; archive resource fallback is disabled");
        return false;
    }

    if (mSources.UseExtractedAssets)
    {
        auto LogProbe = [&](const std::string& ProbePath)
        {
            Core::Utils::Logger::Info("ResourceCache",
                std::string("Bootstrap probe ") + ProbePath +
                (ReadExtractedAsset(ProbePath) ? ": found" : ": missing"));
        };
        LogProbe("units.dat");
        LogProbe("images.tbl");
        LogProbe("images.rel");
        LogProbe("game/consoles/terran/console_left.dds");
        LogProbe("Tileset/badlands.vx4ex");
        LogProbe("Tileset/badlands.dds.vr4");
    }

    auto LoadTable = [&](const std::string& Name, auto LoadFn)
    {
        std::optional<std::vector<uint8_t>> Data;
        if (mSources.IsCasc)
        {
            Data = ReadFile(Name);
            if (!Data) Data = ReadFile("arr/" + Name);
            if (!Data) Data = ReadFile("arr\\" + Name);
            if (!Data) Data = ReadFile("data/" + Name);
            if (!Data) Data = ReadFile("data\\" + Name);
        }
        else Data = ReadWithFallback(Name);
        if (Data) LoadFn(*Data);
        else Core::Utils::Logger::Warning("ResourceCache", "Cannot load: " + Name);
    };

    LoadTable("units.dat",    [&](auto& D) { mData.Loader.LoadUnitDataRaw(D); });
    LoadTable("weapons.dat",  [&](auto& D) { mData.Loader.LoadWeaponDataRaw(D); });
    LoadTable("flingy.dat",   [&](auto& D) { mData.Loader.LoadFlingyDataRaw(D); });
    LoadTable("sprites.dat",  [&](auto& D) { mData.Loader.LoadSpriteDataRaw(D); });
    LoadTable("images.dat",   [&](auto& D) { mData.Loader.LoadImageDataRaw(D); });
    LoadTable("images.rel",   [&](auto& D) { mData.Loader.LoadImageRelationDataRaw(D); });
    LoadTable("upgrades.dat", [&](auto& D) { mData.Loader.LoadUpgradeDataRaw(D); });
    LoadTable("techdata.dat", [&](auto& D) { mData.Loader.LoadTechDataRaw(D); });
    LoadTable("images.tbl",   [&](auto& D) { mData.Loader.LoadImageTblDataRaw(D); });
    mData.Loader.RebuildImageMappingFromTbl();

    auto UnitPal = ReadUnitPalette();
    if (UnitPal) mData.ActivePalette = *UnitPal;
    else mData.ActivePalette = GetDefaultPalette();

    LoadUIGraphics();

    mIsInitialized = true;
    Core::Utils::Logger::Info("ResourceCache", "Initialization complete");
    return true;
}

} // namespace Resource::Graphics


// === Consolidated from src/Resource/ResourceCacheSources.cpp ===


#include "Resource/CascArchive.h"
#include "Resource/FileSystemArchive.h"
#include "Resource/MpqArchive.h"

#include <memory>

namespace Resource::Graphics
{


bool ResourceCache::OpenMpqArchives(const std::string& InstallPath)
{
    auto TryOpen = [&](const std::string& Name, std::unique_ptr<IArchiveReader>& Out) -> bool
    {
        auto Reader = std::make_unique<MpqArchive>();
        std::string FullPath = InstallPath + "/" + Name;
        if (Reader->Open(FullPath)) { Out = std::move(Reader); return true; }
        FullPath = InstallPath + "\\" + Name;
        if (Reader->Open(FullPath)) { Out = std::move(Reader); return true; }
        return false;
    };

    TryOpen("Patch_rt.mpq", mSources.Archive3);
    TryOpen("BrooDat.mpq", mSources.Archive);
    TryOpen("StarDat.mpq", mSources.Archive2);

    mSources.ListfilePath = InstallPath + "/(listfile)";

    auto AttachLf = [&](std::unique_ptr<IArchiveReader>& Arc)
    {
        if (Arc) static_cast<MpqArchive*>(Arc.get())->AddListFile(mSources.ListfilePath);
    };
    AttachLf(mSources.Archive3);
    AttachLf(mSources.Archive);
    AttachLf(mSources.Archive2);

    if (!mSources.Archive && mSources.Archive2) mSources.Archive = std::move(mSources.Archive2);

    if (!mSources.Archive)
    {
        Core::Utils::Logger::Warning("ResourceCache", "No game data at: " + InstallPath);
        return false;
    }
    Core::Utils::Logger::Info("ResourceCache", "Opened game MPQ");
    return true;
}

bool ResourceCache::OpenCascStorage(const std::string& StoragePath)
{
    auto Reader = std::make_unique<CascArchive>();
    if (Reader->Open(StoragePath))
    {
        mSources.Archive = std::move(Reader);
        return true;
    }
    return false;
}

std::vector<std::string> ResourceCache::BuildExtractedAssetCandidates(const std::string& Path) const
{
    std::vector<std::string> Candidates;
    std::unordered_set<std::string> Seen;

    const std::string Normalized = NormalizeLogicalPath(Path);
    const std::string Lower = ToLowerCopy(Normalized);

    AddUniquePath(Candidates, Seen, Normalized);

    const size_t SlashPos = Lower.find('/');
    const bool HasSlash = SlashPos != std::string::npos;
    const size_t DotPos = Lower.rfind('.');
    const std::string Ext = (DotPos == std::string::npos) ? std::string() : Lower.substr(DotPos);

    if (!HasSlash && (Ext == ".dat" || Ext == ".tbl"))
    {
        AddUniquePath(Candidates, Seen, "arr/" + Lower);
        AddUniquePath(Candidates, Seen, "data/" + Lower);
    }

    if (Lower.rfind("game/", 0) == 0)
    {
        const std::string Suffix = Lower.substr(5);
        AddUniquePath(Candidates, Seen, "textures/game/" + Suffix);
        AddUniquePath(Candidates, Seen, "textures/ui/" + Suffix);

        if (Lower == "game/icons.grp")
            AddUniquePath(Candidates, Seen, "game/icons.dds.grp");
    }

    if (Lower.rfind("sprites/", 0) == 0 || Lower.rfind("cursor/", 0) == 0)
        AddUniquePath(Candidates, Seen, "textures/" + Lower);

    if (Lower == "sprites/cmdbtns/cmdicons.grp")
    {
        AddUniquePath(Candidates, Seen, "unit/cmdicons/cmdicons.grp");
        AddUniquePath(Candidates, Seen, "unit/cmdicons/cmdicons.dds.grp");
    }
    else if (Lower == "sprites/cmdbtns/cmdicons.dds.grp")
    {
        AddUniquePath(Candidates, Seen, "unit/cmdicons/cmdicons.dds.grp");
    }
    else if (Lower == "sprites/cmdbtns/ticon.pcx")
    {
        AddUniquePath(Candidates, Seen, "game/ticon.pcx");
    }
    else if (Lower == "sprites/cmdbtns/tcmdbtns.grp")
    {
        AddUniquePath(Candidates, Seen, "game/consoles/terran/cmdbtns.grp");
        AddUniquePath(Candidates, Seen, "game/consoles/terran/cmdbtns.dds.grp");
    }
    else if (Lower == "sprites/cmdbtns/tcmdbtns.dds.grp")
    {
        AddUniquePath(Candidates, Seen, "game/consoles/terran/cmdbtns.dds.grp");
    }
    else if (Lower == "sprites/cmdbtns/pcmdbtns.grp")
    {
        AddUniquePath(Candidates, Seen, "game/consoles/protoss/cmdbtns.grp");
        AddUniquePath(Candidates, Seen, "game/consoles/protoss/cmdbtns.dds.grp");
    }
    else if (Lower == "sprites/cmdbtns/pcmdbtns.dds.grp")
    {
        AddUniquePath(Candidates, Seen, "game/consoles/protoss/cmdbtns.dds.grp");
    }
    else if (Lower == "sprites/cmdbtns/zcmdbtns.grp")
    {
        AddUniquePath(Candidates, Seen, "game/consoles/zerg/cmdbtns.grp");
        AddUniquePath(Candidates, Seen, "game/consoles/zerg/cmdbtns.dds.grp");
    }
    else if (Lower == "sprites/cmdbtns/zcmdbtns.dds.grp")
    {
        AddUniquePath(Candidates, Seen, "game/consoles/zerg/cmdbtns.dds.grp");
    }
    else if (Lower == "sprites/wirefram/wirefram.grp")
    {
        AddUniquePath(Candidates, Seen, "unit/wirefram/wirefram.grp");
        AddUniquePath(Candidates, Seen, "unit/wirefram/wirefram.dds.grp");
    }
    else if (Lower == "sprites/wirefram/wirefram.dds.grp")
    {
        AddUniquePath(Candidates, Seen, "unit/wirefram/wirefram.dds.grp");
    }

    if (Lower.rfind("tileset/", 0) == 0)
    {
        const std::string Suffix = Lower.substr(8);
        AddUniquePath(Candidates, Seen, "textures/tilesets/" + Suffix);
    }

    if (Lower.rfind("unit/", 0) == 0)
    {
        const std::string Suffix = Lower.substr(5);
        AddUniquePath(Candidates, Seen, "textures/sprites/" + Suffix);
    }
    else if (Lower.rfind("terran/", 0) == 0 ||
             Lower.rfind("zerg/", 0) == 0 ||
             Lower.rfind("protoss/", 0) == 0 ||
             Lower.rfind("neutral/", 0) == 0 ||
             Lower.rfind("thingy/", 0) == 0)
    {
        AddUniquePath(Candidates, Seen, "unit/" + Lower);
        AddUniquePath(Candidates, Seen, "textures/sprites/" + Lower);
    }

    if (Ext == ".grp" && Lower.rfind("game/", 0) != 0 && Lower.rfind("tileset/", 0) != 0)
        AddUniquePath(Candidates, Seen, "textures/sprites/" + Lower);

    return Candidates;
}

std::optional<std::vector<uint8_t>> ResourceCache::ReadExtractedAsset(const std::string& Path)
{
    if (!mSources.Archive)
        return std::nullopt;

    const auto Candidates = BuildExtractedAssetCandidates(Path);
    for (const auto& Candidate : Candidates)
    {
        if (!mSources.AssetsRoot.empty())
        {
            const auto FullPath = std::filesystem::path(mSources.AssetsRoot) / std::filesystem::path(Candidate);
            if (!std::filesystem::exists(FullPath))
                continue;
        }

        auto Data = mSources.Archive->ReadFile(Candidate);
        if (Data)
        {
            if (Candidate != NormalizeLogicalPath(Path))
            {
                Core::Utils::Logger::Info("ResourceCache",
                    "Extracted asset remap: " + Path + " -> " + Candidate);
            }
            return Data;
        }
    }

    return std::nullopt;
}

std::optional<std::vector<uint8_t>> ResourceCache::ReadFile(const std::string& Path)
{
    if (mSources.UseExtractedAssets)
        return ReadExtractedAsset(Path);

    if (mSources.IsCasc)
    {
        std::string Resolved = ResolveCascPath(Path);
        if (Resolved.empty())
        {
            if (mSources.Archive)
            {
                auto D = mSources.Archive->ReadFile(Path);
                if (D) return D;
            }
            return std::nullopt;
        }
        return mSources.Archive->ReadFile(Resolved);
    }

    if (mSources.Archive3)
    {
        auto D = mSources.Archive3->ReadFile(Path);
        if (D) return D;
    }
    if (mSources.Archive)
    {
        auto D = mSources.Archive->ReadFile(Path);
        if (D) return D;
    }
    if (mSources.Archive2)
    {
        auto D = mSources.Archive2->ReadFile(Path);
        if (D) return D;
    }
    return std::nullopt;
}

std::vector<std::string> ResourceCache::DebugBuildExtractedAssetCandidates(const std::string& Path) const
{
    return BuildExtractedAssetCandidates(Path);
}

std::optional<std::vector<uint8_t>> ResourceCache::ReadWithFallback(const std::string& Path)
{
    if (mSources.UseExtractedAssets)
        return ReadExtractedAsset(Path);

    auto TryOn = [&](IArchiveReader* Arc, const std::string& P) -> std::optional<std::vector<uint8_t>>
    {
        if (!Arc) return std::nullopt;
        auto D = Arc->ReadFile(P);
        if (D) return D;
        std::string Upper;
        Upper.reserve(P.size());
        for (char C : P) Upper += (char)std::toupper((unsigned char)C);
        D = Arc->ReadFile(Upper);
        if (D) return D;
        std::string Lower;
        Lower.reserve(P.size());
        for (char C : P) Lower += (char)std::tolower((unsigned char)C);
        D = Arc->ReadFile(Lower);
        if (D) return D;
        return std::nullopt;
    };

    auto D = TryOn(mSources.Archive3.get(), Path);
    if (D) return D;
    D = TryOn(mSources.Archive.get(), Path);
    if (D) return D;
    D = TryOn(mSources.Archive2.get(), Path);
    if (D) return D;

    D = TryOn(mSources.Archive3.get(), "arr/" + Path);
    if (D) return D;
    D = TryOn(mSources.Archive.get(), "arr/" + Path);
    if (D) return D;
    D = TryOn(mSources.Archive2.get(), "arr/" + Path);
    if (D) return D;

    D = TryOn(mSources.Archive3.get(), "data/" + Path);
    if (D) return D;
    D = TryOn(mSources.Archive.get(), "data/" + Path);
    if (D) return D;
    D = TryOn(mSources.Archive2.get(), "data/" + Path);
    if (D) return D;

    return std::nullopt;
}

std::string ResourceCache::ResolveCascPath(const std::string& Path)
{
    if (!mSources.Archive || !mSources.Archive->IsOpen()) return Path;

    auto It = mSources.CascPathCache.find(Path);
    if (It != mSources.CascPathCache.end()) return It->second;

    auto SwapSep = [](std::string S) -> std::string {
        for (auto& C : S) { if (C == '/') C = '\\'; else if (C == '\\') C = '/'; }
        return S;
    };
    std::string AltPath = SwapSep(Path);

    for (const auto& Prefix : mSources.CascPrefixes)
    {
        for (const auto& P : { Path, AltPath })
        {
            std::string Candidate = Prefix + P;
            if (mSources.Archive->HasFile(Candidate))
            {
                mSources.CascPathCache[Path] = Candidate;
                return Candidate;
            }
        }
    }

    mSources.CascPathCache[Path] = "";
    return "";
}

bool ResourceCache::OpenAssetsDirectory(const std::string& AssetsPath)
{
    auto Reader = std::make_unique<FileSystemArchive>();
    if (Reader->Open(AssetsPath))
    {
        mSources.Archive = std::move(Reader);
        mSources.Archive2.reset();
        mSources.Archive3.reset();
        mSources.UseExtractedAssets = true;
        mSources.AssetsRoot = NormalizeLogicalPath(AssetsPath);
        Core::Utils::Logger::Info("ResourceCache", "Opened assets directory: " + AssetsPath);
        return true;
    }
    return false;
}

} // namespace Resource::Graphics

