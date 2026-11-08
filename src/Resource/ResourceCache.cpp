#include "Resource/ResourceCache.h"

#include "Core/Utils/Logger.h"
#include "Render/Private/SokolGpuInterop.h"
#include "Resource/GrpToGpu.h"
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

static sg_image UploadRgba(int Width, int Height, const uint8_t* Rgba)
{
    if (Width <= 0 || Height <= 0 || !Rgba) return sg_image{0};

    sg_image_desc Desc = {};
    Desc.width = Width;
    Desc.height = Height;
    Desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    Desc.data.mip_levels[0] = { Rgba, static_cast<size_t>(Width) * static_cast<size_t>(Height) * 4 };
    Desc.label = "grp-frame";

    sg_image Img = sg_make_image(&Desc);
    if (Img.id == 0)
    {
        Core::Utils::Logger::Error("GrpToGpu", "sg_make_image failed for " +
            std::to_string(Width) + "x" + std::to_string(Height));
    }
    return Img;
}

GpuImage UploadRgbaFull(int Width, int Height, const uint8_t* Rgba)
{
    GpuImage Out;
    Out.Width = Width;
    Out.Height = Height;
    if (Rgba && Width > 0 && Height > 0)
    {
        const size_t PixelCount = static_cast<size_t>(Width) * static_cast<size_t>(Height);
        for (size_t I = 0; I < PixelCount; ++I)
        {
            if (Rgba[I * 4u + 3u] != 0)
            {
                Out.HasVisiblePixels = true;
                break;
            }
        }
    }
    Out.Image = Render::FromSokol(UploadRgba(Width, Height, Rgba));
    if (Out.Image.id == 0) return Out;

    sg_view_desc VDesc = {};
    VDesc.texture.image = Render::ToSokol(Out.Image);
    VDesc.label = "grp-view";
    Out.View = Render::FromSokol(sg_make_view(&VDesc));
    if (Out.View.id == 0)
    {
        Core::Utils::Logger::Error("GrpToGpu", "sg_make_view failed");
        sg_destroy_image(Render::ToSokol(Out.Image));
        Out.Image = {};
    }
    return Out;
}

void DestroyImage(GpuImage& Img)
{
    if (Img.TeamColorMask.View.id != 0)
    {
        sg_destroy_view(Render::ToSokol(Img.TeamColorMask.View));
        Img.TeamColorMask.View = {};
    }
    if (Img.TeamColorMask.Image.id != 0)
    {
        sg_destroy_image(Render::ToSokol(Img.TeamColorMask.Image));
        Img.TeamColorMask.Image = {};
    }
    if (Img.View.id != 0) { sg_destroy_view(Render::ToSokol(Img.View)); Img.View = {}; }
    if (Img.Image.id != 0) { sg_destroy_image(Render::ToSokol(Img.Image)); Img.Image = {}; }
    Img.Width = 0;
    Img.Height = 0;
}

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


#include "Resource/IArchiveReader.h"

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



// === Consolidated from src/Resource/ResourceCachePortrait.cpp ===
#include "Resource/GrpToGpu.h"
#include "Resource/PortraitVideoDecoder.h"
#include "Resource/RgbaImageFileDecoder.h"

#include <cmath>
#include <filesystem>
#include <utility>

namespace Resource::Graphics
{

namespace
{
    PortraitFrame UploadPortraitFrame(const DecodedVideoFrame& DecodedFrame)
    {
        PortraitFrame Frame = {};
        Frame.Image = UploadRgbaFull(DecodedFrame.Image.Width,
                                     DecodedFrame.Image.Height,
                                     DecodedFrame.Image.Rgba.data());
        Frame.DurationSeconds = DecodedFrame.DurationSeconds;
        return Frame;
    }

    bool SetStreamingPortraitFrame(PortraitClip& Clip, const DecodedVideoFrame& DecodedFrame)
    {
        PortraitFrame NewFrame = UploadPortraitFrame(DecodedFrame);
        if (!NewFrame.Image.Valid())
            return false;

        DestroyImage(Clip.CurrentFrame.Image);
        Clip.CurrentFrame = std::move(NewFrame);
        Clip.Width = std::max(Clip.Width, DecodedFrame.Image.Width);
        Clip.Height = std::max(Clip.Height, DecodedFrame.Image.Height);
        return true;
    }

    bool OpenStreamingPortraitSegment(PortraitClip& Clip, size_t StartIndex)
    {
        if (Clip.WebmPaths.empty())
            return false;

        for (size_t Attempt = 0; Attempt < Clip.WebmPaths.size(); ++Attempt)
        {
            const size_t Index = (StartIndex + Attempt) % Clip.WebmPaths.size();
            Clip.WebmDecoder.Close();
            if (!Clip.WebmDecoder.Open(Clip.WebmPaths[Index]))
                continue;

            DecodedVideoFrame Frame = {};
            if (!Clip.WebmDecoder.ReadNextFrame(Frame))
                continue;

            if (!SetStreamingPortraitFrame(Clip, Frame))
                continue;

            Clip.ActiveWebmPath = Index;
            return true;
        }

        return false;
    }

    const PortraitFrame* StaticPortraitFrameAt(const PortraitClip& Clip, double TimeSeconds)
    {
        if (Clip.Frames.empty())
            return nullptr;

        double TotalSeconds = 0.0;
        for (const auto& Frame : Clip.Frames)
            TotalSeconds += std::max(0.01f, Frame.DurationSeconds);

        double LocalTime = TotalSeconds > 0.0 ? std::fmod(TimeSeconds, TotalSeconds) : 0.0;
        for (const auto& Frame : Clip.Frames)
        {
            LocalTime -= std::max(0.01f, Frame.DurationSeconds);
            if (LocalTime <= 0.0)
                return &Frame;
        }

        return &Clip.Frames.back();
    }

    const PortraitFrame* StreamingPortraitFrameAt(PortraitClip& Clip, double TimeSeconds)
    {
        if (!Clip.CurrentFrame.Image.Valid())
            return nullptr;

        if (Clip.NextFrameTimeSeconds <= 0.0 || TimeSeconds < Clip.NextFrameTimeSeconds - 1.0)
        {
            Clip.NextFrameTimeSeconds = TimeSeconds + std::max(0.01f, Clip.CurrentFrame.DurationSeconds);
            return &Clip.CurrentFrame;
        }

        // Drop stale backlog immediately so selecting/box-selecting units never stalls
        if (TimeSeconds > Clip.NextFrameTimeSeconds + 0.15)
        {
            Clip.NextFrameTimeSeconds = TimeSeconds;
        }

        if (TimeSeconds >= Clip.NextFrameTimeSeconds)
        {
            DecodedVideoFrame Frame = {};
            if (!Clip.WebmDecoder.ReadNextFrame(Frame))
            {
                const size_t NextSegment = Clip.WebmPaths.empty() ? 0 : (Clip.ActiveWebmPath + 1) % Clip.WebmPaths.size();
                OpenStreamingPortraitSegment(Clip, NextSegment);
            }
            else
            {
                SetStreamingPortraitFrame(Clip, Frame);
            }

            Clip.NextFrameTimeSeconds = TimeSeconds + std::max(0.01f, Clip.CurrentFrame.DurationSeconds);
        }

        return Clip.CurrentFrame.Image.Valid() ? &Clip.CurrentFrame : nullptr;
    }
}

const PortraitClip* ResourceCache::GetPortraitClip(const std::string& PortraitKey)
{
    if (PortraitKey.empty())
        return nullptr;

    const std::string Key = ToLowerCopy(PortraitKey);
    if (auto It = mUI.PortraitClips.find(Key); It != mUI.PortraitClips.end())
        return It->second.Valid() ? &It->second : nullptr;

    PortraitClip Clip = {};
    if (!LoadPortraitClip(Key, Clip))
    {
        mUI.PortraitClips.emplace(Key, PortraitClip{});
        return nullptr;
    }

    auto [It, Inserted] = mUI.PortraitClips.emplace(Key, std::move(Clip));
    (void)Inserted;
    return It->second.Valid() ? &It->second : nullptr;
}

const PortraitFrame* ResourceCache::GetPortraitFrame(const std::string& PortraitKey, double TimeSeconds)
{
    if (PortraitKey.empty())
        return nullptr;

    const std::string Key = ToLowerCopy(PortraitKey);
    auto It = mUI.PortraitClips.find(Key);
    if (It == mUI.PortraitClips.end())
    {
        PortraitClip Clip = {};
        if (!LoadPortraitClip(Key, Clip))
        {
            mUI.PortraitClips.emplace(Key, PortraitClip{});
            return nullptr;
        }

        It = mUI.PortraitClips.emplace(Key, std::move(Clip)).first;
    }

    PortraitClip& Clip = It->second;
    if (!Clip.Valid())
        return nullptr;

    if (Clip.StreamingWebm)
        return StreamingPortraitFrameAt(Clip, TimeSeconds);
    return StaticPortraitFrameAt(Clip, TimeSeconds);
}

bool ResourceCache::LoadPortraitClip(const std::string& PortraitKey, PortraitClip& OutClip)
{
#ifdef _WIN32
    if (!mSources.UseExtractedAssets || mSources.AssetsRoot.empty())
        return false;

    std::filesystem::path PortraitDir = std::filesystem::path(mSources.AssetsRoot) / "portrait" / PortraitKey;
    if (!std::filesystem::exists(PortraitDir) || !std::filesystem::is_directory(PortraitDir))
        return false;

    std::vector<std::filesystem::path> Clips;
    for (const auto& Entry : std::filesystem::directory_iterator(PortraitDir))
    {
        if (!Entry.is_regular_file())
            continue;

        const std::string FileName = ToLowerCopy(Entry.path().filename().string());
        if (ToLowerCopy(Entry.path().extension().string()) == ".webm" &&
            FileName.find("fid") != std::string::npos)
            Clips.push_back(Entry.path());
    }

    std::sort(Clips.begin(), Clips.end(), [](const auto& A, const auto& B)
    {
        return ToLowerCopy(A.filename().string()) < ToLowerCopy(B.filename().string());
    });

    PortraitClip Clip = {};
    if (!Clips.empty())
    {
        Clip.StreamingWebm = true;
        Clip.WebmPaths = Clips;
        if (OpenStreamingPortraitSegment(Clip, 0))
        {
            size_t EncodedFrames = Clip.WebmDecoder.EncodedFrameCount();
            for (size_t I = 1; I < Clip.WebmPaths.size(); ++I)
            {
                WebmVideoDecoder Probe;
                if (Probe.Open(Clip.WebmPaths[I]))
                    EncodedFrames += Probe.EncodedFrameCount();
            }

            Core::Utils::Logger::Info("ResourceCache",
                "Loaded portrait clip " + PortraitKey + ": streaming " +
                std::to_string(Clip.WebmPaths.size()) + " webm files, " +
                std::to_string(EncodedFrames) + " encoded frames");
            OutClip = std::move(Clip);
            return true;
        }

        Core::Utils::Logger::Warning("ResourceCache",
            "Failed to open streaming portrait webm clip: " + PortraitKey);
        DestroyImage(Clip.CurrentFrame.Image);
        Clip = {};
    }

    if (!Clip.Valid())
    {
        std::vector<std::filesystem::path> ImageFrames;
        for (const auto& Entry : std::filesystem::directory_iterator(PortraitDir))
        {
            if (!Entry.is_regular_file())
                continue;

            const std::string FileName = ToLowerCopy(Entry.path().filename().string());
            const std::string Ext = ToLowerCopy(Entry.path().extension().string());
            if (FileName.find("fid") == std::string::npos)
                continue;
            if (Ext == ".png" || Ext == ".jpg" || Ext == ".jpeg")
                ImageFrames.push_back(Entry.path());
        }

        std::sort(ImageFrames.begin(), ImageFrames.end(), [](const auto& A, const auto& B)
        {
            return ToLowerCopy(A.filename().string()) < ToLowerCopy(B.filename().string());
        });

        for (const auto& ImagePath : ImageFrames)
        {
            auto ImageOpt = DecodeRgbaImageFile(ImagePath);
            if (!ImageOpt || ImageOpt->Rgba.empty())
            {
                Core::Utils::Logger::Warning("ResourceCache",
                    "Failed to decode portrait image: " + ImagePath.string());
                continue;
            }

            PortraitFrame Frame = {};
            Frame.Image = UploadRgbaFull(ImageOpt->Width, ImageOpt->Height, ImageOpt->Rgba.data());
            Frame.DurationSeconds = 1.0f / 12.0f;
            if (!Frame.Image.Valid())
                continue;

            Clip.Width = std::max(Clip.Width, ImageOpt->Width);
            Clip.Height = std::max(Clip.Height, ImageOpt->Height);
            Clip.Frames.push_back(std::move(Frame));
        }
    }

    if (!Clip.Valid())
        return false;

    Core::Utils::Logger::Info("ResourceCache",
        "Loaded portrait clip " + PortraitKey + ": " + std::to_string(Clip.Frames.size()) + " frames");
    OutClip = std::move(Clip);
    return true;
#else
    (void)PortraitKey;
    (void)OutClip;
    return false;
#endif
}
} // namespace Resource::Graphics



// === Consolidated from src/Resource/ResourceCacheSprites.cpp ===
#include "Resource/LightTable.h"

#include <cstdlib>

namespace Resource::Graphics
{

namespace
{
    bool PreferClassicEffectSprite(const std::string& GrpPath)
    {
        std::string Lower = ToLowerCopy(GrpPath);
        std::replace(Lower.begin(), Lower.end(), '\\', '/');
        return Lower.rfind("thingy/", 0) == 0 ||
               Lower.rfind("bullet/", 0) == 0;
    }

    bool ShouldLogSpriteLoads()
    {
        const char* Value = std::getenv("STARCRAFT_DEBUG_SPRITE_LOADS");
        return Value && Value[0] == '1';
    }

    bool ForceClassicSprites()
    {
        const char* Value = std::getenv("STARCRAFT_FORCE_CLASSIC_SPRITES");
        return Value && Value[0] != '\0' && Value[0] != '0';
    }

    bool HasVisiblePixels(const std::vector<uint8_t>& Rgba)
    {
        for (size_t I = 3; I < Rgba.size(); I += 4)
            if (Rgba[I] != 0)
                return true;
        return false;
    }

    RawColor LightTableTint(uint8_t ColorShift)
    {
        // images.dat color_shift selects ofire/gfire/bfire/bexpl/trans50/red/
        // green. Alpha still comes from the GRP's light-table row index.
        switch (ColorShift)
        {
            case 1: return RawColor(255, 128, 32, 255);
            case 2: return RawColor(80, 255, 96, 255);
            case 3: return RawColor(64, 144, 255, 255);
            case 4: return RawColor(128, 192, 255, 255);
            case 6: return RawColor(255, 72, 72, 255);
            case 7: return RawColor(72, 255, 96, 255);
            default: return RawColor(255, 255, 255, 255);
        }
    }

    uint8_t LightTableIndexCount(uint8_t ColorShift)
    {
        // All extracted tilesets use the same row counts for these lookup
        // tables: ofire, gfire, bfire, bexpl, trans50, red, green.
        switch (ColorShift)
        {
            case 1: return 63;
            case 2: return 32;
            case 3: return 40;
            case 4: return 63;
            case 5: return 255;
            case 6:
            case 7: return 1;
            default: return 63;
        }
    }

    const char* LightTableFileName(uint8_t ColorShift)
    {
        static constexpr const char* Names[] = {
            "ofire", "gfire", "bfire", "bexpl", "trans50", "red", "green"
        };
        if (ColorShift < 1 || ColorShift > 7)
            return nullptr;
        return Names[ColorShift - 1];
    }

    std::string DescribeRgba(const std::vector<uint8_t>& Rgba)
    {
        int Visible = 0;
        int NonBlack = 0;
        for (size_t I = 0; I + 3 < Rgba.size(); I += 4)
        {
            if (Rgba[I + 3] != 0)
            {
                ++Visible;
                if (Rgba[I] != 0 || Rgba[I + 1] != 0 || Rgba[I + 2] != 0)
                    ++NonBlack;
            }
        }
        return "visible=" + std::to_string(Visible) +
               " nonblack=" + std::to_string(NonBlack) +
               " pixels=" + std::to_string(Rgba.size() / 4);
    }

    std::vector<uint8_t> ScaleMaskRgbaNearest(const DecodedFrame& Source, int Width, int Height)
    {
        if (Source.TeamColorMaskRgba.empty() ||
            Source.Width <= 0 || Source.Height <= 0 ||
            Width <= 0 || Height <= 0)
        {
            return {};
        }

        std::vector<uint8_t> Mask((size_t)Width * (size_t)Height * 4u, 0);
        for (int Y = 0; Y < Height; ++Y)
        {
            const int SrcY = std::min(Source.Height - 1, (Y * Source.Height) / Height);
            for (int X = 0; X < Width; ++X)
            {
                const int SrcX = std::min(Source.Width - 1, (X * Source.Width) / Width);
                const size_t Src = ((size_t)SrcY * (size_t)Source.Width + (size_t)SrcX) * 4u;
                const size_t Dst = ((size_t)Y * (size_t)Width + (size_t)X) * 4u;
                Mask[Dst + 0] = Source.TeamColorMaskRgba[Src + 0];
                Mask[Dst + 1] = Source.TeamColorMaskRgba[Src + 1];
                Mask[Dst + 2] = Source.TeamColorMaskRgba[Src + 2];
                Mask[Dst + 3] = Source.TeamColorMaskRgba[Src + 3];
            }
        }
        return Mask;
    }

    void AttachTeamColorMask(GpuImage& Frame, int Width, int Height, const std::vector<uint8_t>& MaskRgba)
    {
        if (MaskRgba.empty())
            return;

        GpuImage Mask = UploadRgbaFull(Width, Height, MaskRgba.data());
        Frame.TeamColorMask = Mask;
    }
}

const std::vector<GpuImage>* ResourceCache::GetSpriteFrames(uint32_t ImageID)
{
    auto It = mSprites.FramesByImageId.find(ImageID);
    if (It != mSprites.FramesByImageId.end()) return &It->second;

    std::string GrpPath = mData.Loader.ResolveGrpPath(ImageID);
    if (GrpPath.empty()) return nullptr;

    return LoadSpriteFramesForPath(ImageID, GrpPath);
}

const std::vector<GpuImage>* ResourceCache::GetDatImageFrames(uint32_t ImageID)
{
    constexpr uint32_t kDatImageCacheFlag = 0x80000000u;
    const uint32_t CacheKey = kDatImageCacheFlag | ImageID;
    auto It = mSprites.FramesByImageId.find(CacheKey);
    if (It != mSprites.FramesByImageId.end()) return &It->second;

    const auto* Relation = mData.Loader.FindImageRelationByID(ImageID);
    const bool IsShadowSprite = Relation && Relation->IsShadow();
    GrpDecodeOptions DecodeOptions;
    if (const auto* Image = mData.Loader.FindImageByID(ImageID);
        Image && Image->Modifier == 9)
    {
        DecodeOptions.DecodeLightTableMask = true;
        DecodeOptions.GenerateTeamColorMask = false;
        DecodeOptions.LightTableTint = LightTableTint(Image->ColorShift);
        DecodeOptions.LightTableIndexCount =
            LightTableIndexCount(Image->ColorShift);

        const char* TableName = LightTableFileName(Image->ColorShift);
        if (TableName && !mTilesets.ActiveName.empty())
        {
            auto Cached = mTilesets.LightTableRgbaByColorShift.find(Image->ColorShift);
            if (Cached != mTilesets.LightTableRgbaByColorShift.end())
            {
                DecodeOptions.LightTableRgba = Cached->second;
                DecodeOptions.UseLightTableRgba = true;
            }
            else
            {
                const std::string TilesetBase = "Tileset/" + mTilesets.ActiveName;
                auto TableData = ReadWithFallback(
                    TilesetBase + "/" + TableName + ".pcx");
                auto PaletteData = ReadWithFallback(TilesetBase + ".wpe");
                if (TableData && PaletteData)
                {
                    const IndexedPcx Table = DecodeIndexedPcx(*TableData);
                    const auto TilesetPalette = DecodeWpe(*PaletteData);
                    if (TilesetPalette && Table.Valid() && Table.Width == 256)
                    {
                        DecodeOptions.LightTableRgba = BuildLightTableRgba(
                            Table.Pixels, Table.Width, Table.Height, *TilesetPalette);
                        DecodeOptions.UseLightTableRgba = true;
                        mTilesets.LightTableRgbaByColorShift.emplace(
                            Image->ColorShift, DecodeOptions.LightTableRgba);
                    }
                }
            }
        }
    }

    std::string GrpPath = mData.Loader.ResolveImageGrpPath(ImageID);
    if (GrpPath.empty()) return nullptr;

    std::string NormalizedGrpPath = ToLowerCopy(GrpPath);
    std::replace(NormalizedGrpPath.begin(), NormalizedGrpPath.end(), '\\', '/');
    if (NormalizedGrpPath == "protoss/texture.grp")
    {
        DecodeOptions.GenerateTeamColorMask = false;
        DecodeOptions.TransparentIndexZero = false;
    }

    // images.dat also contains links to LO coordinate tables (for example the
    // Nexus large-shield table). They are reached by valid iscript next-image
    // opcodes but are not GRP bitmaps. Feeding their binary offsets into the
    // GRP decoder produced the translucent ring seen under Protoss bases.
    // Similarly, bullet/circle14.grp is classic StarCraft's invisible projectile /
    // collision delivery mask that must never be uploaded as an in-world sprite.
    const std::string LowerGrpPath = ToLowerCopy(GrpPath);
    if (LowerGrpPath.size() < 4 ||
        LowerGrpPath.substr(LowerGrpPath.size() - 4) != ".grp" ||
        LowerGrpPath.find("circle14") != std::string::npos)
    {
        return nullptr;
    }

    const std::vector<GpuImage>* Frames = LoadSpriteFramesForPath(
        CacheKey, GrpPath, "DAT image", DecodeOptions);
    if (IsShadowSprite && Frames)
    {
        auto CacheIt = mSprites.FramesByImageId.find(CacheKey);
        if (CacheIt != mSprites.FramesByImageId.end())
            for (auto& Frame : CacheIt->second)
                Frame.ShadowSprite = true;
    }
    return Frames;
}

const std::vector<GpuImage>* ResourceCache::GetSpriteOverlayFrames(uint32_t SpriteID)
{
    const auto* Sprite = mData.Loader.FindSpriteByID(SpriteID);
    if (!Sprite)
        return nullptr;
    return GetDatImageFrames(Sprite->ImageID);
}

const std::vector<GpuImage>* ResourceCache::LoadSpriteFramesForPath(uint32_t CacheKey,
                                                                    const std::string& GrpPath,
                                                                    const char* DebugLabel,
                                                                    const GrpDecodeOptions& Options)
{
    auto ReadClassicGrpData = [&]() -> std::optional<std::vector<uint8_t>>
    {
        std::optional<std::vector<uint8_t>> GrpData;
        if (mSources.IsCasc)
        {
            GrpData = ReadFile(GrpPath);
            if (!GrpData) GrpData = ReadFile("unit/" + GrpPath);
            if (!GrpData) GrpData = ReadFile("unit\\" + GrpPath);
            if (!GrpData) GrpData = ReadFile("arr/" + GrpPath);
            if (!GrpData) GrpData = ReadFile("arr\\" + GrpPath);
            if (!GrpData) GrpData = ReadFile("data/" + GrpPath);
            if (!GrpData) GrpData = ReadFile("data\\" + GrpPath);
        }
        else
        {
            GrpData = ReadWithFallback(GrpPath);
            if (!GrpData) GrpData = ReadWithFallback("unit/" + GrpPath);
            if (!GrpData) GrpData = ReadWithFallback("arr/" + GrpPath);
            if (!GrpData) GrpData = ReadWithFallback("data/" + GrpPath);
        }
        return GrpData;
    };

    if (!ForceClassicSprites() &&
        !PreferClassicEffectSprite(GrpPath) &&
        GrpPath.size() > 4 &&
        ToLowerCopy(GrpPath.substr(GrpPath.size() - 4)) == ".grp")
    {
        const std::string HdPath = GrpPath.substr(0, GrpPath.size() - 4) + ".dds.grp";
        auto HdData = ReadFile(HdPath);
        if (HdData)
        {
            auto HdFrames = LoadDdsGrpRgbaFrames(*HdData);
            if (HdFrames && !HdFrames->empty())
            {
                bool HdHasTransparentOnlyFrame = false;
                for (const auto& FrameData : *HdFrames)
                {
                    if (!HasVisiblePixels(FrameData.Rgba))
                    {
                        HdHasTransparentOnlyFrame = true;
                        break;
                    }
                }

                if (HdHasTransparentOnlyFrame)
                {
                    Core::Utils::Logger::Warning("ResourceCache",
                        "HD unit sprite contains a fully transparent frame: " + HdPath +
                        "; falling back to classic GRP");
                }
                else
                {
                std::optional<DecodedSprite> ClassicSprite;
                if (auto ClassicGrpData = ReadClassicGrpData())
                    ClassicSprite = DecodeGrpToRgba(
                        *ClassicGrpData, mData.ActivePalette, Options);

                auto& Out = mSprites.FramesByImageId[CacheKey];
                Out.reserve(HdFrames->size());
                for (size_t I = 0; I < HdFrames->size(); ++I)
                {
                    const auto& FrameData = (*HdFrames)[I];
                    GpuImage Frame = UploadRgbaFull(FrameData.Width, FrameData.Height, FrameData.Rgba.data());
                    if (ClassicSprite && I < ClassicSprite->Frames.size())
                    {
                        const auto Mask = ScaleMaskRgbaNearest(
                            ClassicSprite->Frames[I], FrameData.Width, FrameData.Height);
                        AttachTeamColorMask(Frame, FrameData.Width, FrameData.Height, Mask);
                    }
                    Out.push_back(Frame);
                }

                    if (ShouldLogSpriteLoads())
                    {
                        Core::Utils::Logger::Info("ResourceCache",
                            std::string("Loaded HD unit sprite: ") +
                            (DebugLabel ? DebugLabel : "ImageID") + "=" + std::to_string(CacheKey) +
                            " -> " + HdPath + " (" + std::to_string(Out.size()) + " frames)");
                    }
                    return &Out;
                }
            }

            Core::Utils::Logger::Warning("ResourceCache",
                "Failed to decode HD unit sprite: " + HdPath +
                "; falling back to classic GRP");
        }
    }

    std::optional<std::vector<uint8_t>> GrpData = ReadClassicGrpData();
    if (!GrpData)
    {
        Core::Utils::Logger::Warning("ResourceCache",
            "GRP not found: " + GrpPath + " (cacheKey=" + std::to_string(CacheKey) + ")");
        return nullptr;
    }

    auto SpriteOpt = DecodeGrpToRgba(*GrpData, mData.ActivePalette, Options);
    if (!SpriteOpt)
    {
        Core::Utils::Logger::Warning("ResourceCache",
            "Failed to decode GRP: " + GrpPath);
        return nullptr;
    }

    auto& Out = mSprites.FramesByImageId[CacheKey];
    Out.reserve(SpriteOpt->FrameCount());
    for (auto& F : SpriteOpt->Frames)
    {
        GpuImage Frame = UploadRgbaFull(F.Width, F.Height, F.Rgba.data());
        AttachTeamColorMask(Frame, F.Width, F.Height, F.TeamColorMaskRgba);
        Out.push_back(Frame);
    }

    if (ShouldLogSpriteLoads() && (DebugLabel || (CacheKey >= 100 && CacheKey <= 103)))
    {
        const std::string FirstFrameStats = SpriteOpt->Frames.empty()
            ? std::string("empty")
            : DescribeRgba(SpriteOpt->Frames.front().Rgba);
        Core::Utils::Logger::Info("ResourceCache",
            "Resource GRP: " + std::string(DebugLabel ? DebugLabel : "ImageID") +
            "=" + std::to_string(CacheKey) +
            " -> " + GrpPath + " (" + std::to_string(Out.size()) + " frames, " +
            FirstFrameStats + ")");
    }
    return &Out;
}

} // namespace Resource::Graphics


// === Consolidated from src/Resource/ResourceCacheTilesetImages.cpp ===
#include "Resource/Palette.h"

#include <cstring>

namespace Resource::Graphics
{

namespace
{
std::optional<TilesetAtlas> BuildAtlasFromFrames(const std::vector<DecodedRgbaImage>& Frames)
{
    if (Frames.empty())
        return std::nullopt;

    int FrameW = 0;
    int FrameH = 0;
    for (const auto& Frame : Frames)
    {
        FrameW = std::max(FrameW, Frame.Width);
        FrameH = std::max(FrameH, Frame.Height);
    }
    if (FrameW <= 0 || FrameH <= 0)
        return std::nullopt;

    const int Count = (int)Frames.size();
    const int CellW = FrameW + 2;
    const int CellH = FrameH + 2;
    const int Columns = std::max(1, (int)std::ceil(std::sqrt((double)Count)));
    const int Rows = (Count + Columns - 1) / Columns;
    const int AtlasW = Columns * CellW;
    const int AtlasH = Rows * CellH;
    std::vector<uint8_t> Pixels((size_t)AtlasW * (size_t)AtlasH * 4u, 0);

    for (int I = 0; I < Count; ++I)
    {
        const auto& Frame = Frames[(size_t)I];
        if (Frame.Width <= 0 || Frame.Height <= 0 || Frame.Rgba.empty())
            continue;

        const int DstX = (I % Columns) * CellW + 1;
        const int DstY = (I / Columns) * CellH + 1;
        for (int Y = 0; Y < Frame.Height; ++Y)
        {
            const size_t SrcOff = (size_t)Y * (size_t)Frame.Width * 4u;
            const size_t DstOff =
                ((size_t)(DstY + Y) * (size_t)AtlasW + (size_t)DstX) * 4u;
            std::memcpy(&Pixels[DstOff], &Frame.Rgba[SrcOff], (size_t)Frame.Width * 4u);
        }

        for (int Y = 0; Y < Frame.Height; ++Y)
        {
            const size_t LeftSrc = ((size_t)(DstY + Y) * (size_t)AtlasW + (size_t)DstX) * 4u;
            const size_t LeftDst = ((size_t)(DstY + Y) * (size_t)AtlasW + (size_t)(DstX - 1)) * 4u;
            std::memcpy(&Pixels[LeftDst], &Pixels[LeftSrc], 4u);

            const size_t RightSrc =
                ((size_t)(DstY + Y) * (size_t)AtlasW + (size_t)(DstX + Frame.Width - 1)) * 4u;
            const size_t RightDst =
                ((size_t)(DstY + Y) * (size_t)AtlasW + (size_t)(DstX + Frame.Width)) * 4u;
            std::memcpy(&Pixels[RightDst], &Pixels[RightSrc], 4u);
        }

        for (int X = -1; X <= Frame.Width; ++X)
        {
            const size_t TopSrc = ((size_t)DstY * (size_t)AtlasW + (size_t)(DstX + X)) * 4u;
            const size_t TopDst = ((size_t)(DstY - 1) * (size_t)AtlasW + (size_t)(DstX + X)) * 4u;
            std::memcpy(&Pixels[TopDst], &Pixels[TopSrc], 4u);

            const size_t BottomSrc =
                ((size_t)(DstY + Frame.Height - 1) * (size_t)AtlasW + (size_t)(DstX + X)) * 4u;
            const size_t BottomDst =
                ((size_t)(DstY + Frame.Height) * (size_t)AtlasW + (size_t)(DstX + X)) * 4u;
            std::memcpy(&Pixels[BottomDst], &Pixels[BottomSrc], 4u);
        }
    }

    TilesetAtlas Atlas;
    Atlas.Image = UploadRgbaFull(AtlasW, AtlasH, Pixels.data());
    if (!Atlas.Image.Valid())
        return std::nullopt;
    Atlas.FrameWidth = FrameW;
    Atlas.FrameHeight = FrameH;
    Atlas.CellWidth = CellW;
    Atlas.CellHeight = CellH;
    Atlas.Columns = Columns;
    Atlas.FrameCount = Count;
    return Atlas;
}

}

bool ResourceCache::BuildTilesetImages(const std::string& TilesetName)
{
    auto* Ts = LoadTilesetData(TilesetName);
    if (!Ts || !Ts->Loaded) return false;

    auto& Images = mTilesets.ImagesByName[TilesetName];
    Images.clear();

    auto TryRead = [&](const std::string& Base) -> std::optional<std::vector<uint8_t>>
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
        return ReadFile(Lower);
    };

    auto HdMegatileData = TryRead("Tileset/" + TilesetName + ".dds.vr4");
    if (!HdMegatileData)
    {
        Core::Utils::Logger::Warning("ResourceCache",
            "HD tileset megatile file not found for Tileset/" + TilesetName + ".dds.vr4");
        return false;
    }

#ifdef _WIN32
    auto HdFrames = LoadDdsGrpRgbaFrames(*HdMegatileData);
    if (HdFrames && !HdFrames->empty())
    {
        Images.reserve(HdFrames->size());
        for (const auto& Frame : *HdFrames)
        {
            Images.push_back(UploadRgbaFull(
                Frame.Width, Frame.Height, Frame.Rgba.data()));
        }

        Core::Utils::Logger::Info("ResourceCache",
            "Loaded HD tileset megatiles from Tileset/" + TilesetName + ".dds.vr4: " +
            std::to_string(Images.size()) + " frames");
        return !Images.empty();
    }
    Core::Utils::Logger::Warning("ResourceCache",
        "Failed to decode HD tileset megatiles from Tileset/" + TilesetName + ".dds.vr4");
#else
    Core::Utils::Logger::Info("ResourceCache",
        "HD tileset megatile decode is unavailable on this platform for Tileset/" +
        TilesetName + ".dds.vr4");
#endif
    return false;
}

bool ResourceCache::BuildTilesetAtlas(const std::string& TilesetName)
{
    return BuildTilesetAtlasForMap(TilesetName, nullptr);
}

bool ResourceCache::BuildTilesetAtlasForMap(const std::string& TilesetName,
                                            const std::vector<uint16_t>* MegaTileIds)
{
    auto* Ts = LoadTilesetData(TilesetName);
    if (!Ts || !Ts->Loaded) return false;

    auto TryRead = [&](const std::string& Base) -> std::optional<std::vector<uint8_t>>
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
        return ReadFile(Lower);
    };

    auto HdMegatileData = TryRead("Tileset/" + TilesetName + ".dds.vr4");
    if (!HdMegatileData)
    {
        Core::Utils::Logger::Warning("ResourceCache",
            "HD tileset atlas file not found for Tileset/" + TilesetName + ".dds.vr4");
        return false;
    }

#ifdef _WIN32
    auto Parsed = ParseDdsGrp(*HdMegatileData);
    if (!Parsed || Parsed->Frames.empty())
    {
        Core::Utils::Logger::Warning("ResourceCache",
            "Failed to parse HD tileset header for Tileset/" + TilesetName + ".dds.vr4");
        return false;
    }

    const int TotalFrames = static_cast<int>(Parsed->Frames.size());
    std::vector<bool> Needed(TotalFrames, false);
    Needed[0] = true; // Always include tile 0 (blank/fallback)

    // Include CV5 group 1 creep megatiles (16 variants)
    if (Ts->Cv5.size() >= 32)
    {
        for (size_t Variant = 0; Variant < 16; ++Variant)
        {
            uint16_t MegaIdx = Ts->Cv5[16u + Variant];
            if (MegaIdx < TotalFrames)
                Needed[MegaIdx] = true;
        }
    }

    if (MegaTileIds && !MegaTileIds->empty())
    {
        for (uint16_t RawTile : *MegaTileIds)
        {
            uint16_t MegaIdx = RawTile;
            if (!Ts->Cv5.empty())
            {
                uint16_t Group = (RawTile >> 4) & 0x7FF;
                uint16_t Sub = RawTile & 0xF;
                size_t Cv5Off = (size_t)Group * 16 + Sub;
                if (Cv5Off < Ts->Cv5.size())
                    MegaIdx = Ts->Cv5[Cv5Off];
            }
            if (MegaIdx < TotalFrames)
                Needed[MegaIdx] = true;
        }
    }
    else
    {
        // Fallback: full atlas if no map tiles specified
        std::fill(Needed.begin(), Needed.end(), true);
    }

    std::vector<int> MegaIdxToSlot(TotalFrames, -1);
    std::vector<uint16_t> SlotToMegaIdx;
    SlotToMegaIdx.reserve(TotalFrames);
    for (int I = 0; I < TotalFrames; ++I)
    {
        if (Needed[I])
        {
            MegaIdxToSlot[I] = static_cast<int>(SlotToMegaIdx.size());
            SlotToMegaIdx.push_back(static_cast<uint16_t>(I));
        }
    }

    const int FrameCount = static_cast<int>(SlotToMegaIdx.size());
    if (FrameCount == 0)
        return false;

    int FrameW = Parsed->Frames[0].Width;
    int FrameH = Parsed->Frames[0].Height;
    for (uint16_t MegaIdx : SlotToMegaIdx)
    {
        FrameW = std::max(FrameW, Parsed->Frames[MegaIdx].Width);
        FrameH = std::max(FrameH, Parsed->Frames[MegaIdx].Height);
    }
    if (FrameW <= 0 || FrameH <= 0)
        return false;

    const int CellW = FrameW + 2;
    const int CellH = FrameH + 2;
    const int Columns = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<double>(FrameCount)))));
    const int Rows = (FrameCount + Columns - 1) / Columns;
    const int AtlasW = Columns * CellW;
    const int AtlasH = Rows * CellH;
    std::vector<uint8_t> Pixels(static_cast<size_t>(AtlasW) * static_cast<size_t>(AtlasH) * 4u, 0);

    for (int Slot = 0; Slot < FrameCount; ++Slot)
    {
        uint16_t MegaIdx = SlotToMegaIdx[Slot];
        const auto& FrameInfo = Parsed->Frames[MegaIdx];
        auto ImageOpt = LoadWicRgbaImageFromMemory(
            HdMegatileData->data() + FrameInfo.DataOffset,
            FrameInfo.ByteSize);
        if (!ImageOpt || ImageOpt->Width <= 0 || ImageOpt->Height <= 0 || ImageOpt->Rgba.empty())
            continue;

        const int DstX = (Slot % Columns) * CellW + 1;
        const int DstY = (Slot / Columns) * CellH + 1;
        const int CurW = ImageOpt->Width;
        const int CurH = ImageOpt->Height;

        for (int Y = 0; Y < CurH; ++Y)
        {
            const size_t SrcOff = (size_t)Y * (size_t)CurW * 4u;
            const size_t DstOff = ((size_t)(DstY + Y) * (size_t)AtlasW + (size_t)DstX) * 4u;
            std::memcpy(&Pixels[DstOff], &ImageOpt->Rgba[SrcOff], (size_t)CurW * 4u);
        }

        // 1-pixel border replication for bilinear filtering
        for (int Y = 0; Y < CurH; ++Y)
        {
            const size_t LeftSrc = ((size_t)(DstY + Y) * (size_t)AtlasW + (size_t)DstX) * 4u;
            const size_t LeftDst = ((size_t)(DstY + Y) * (size_t)AtlasW + (size_t)(DstX - 1)) * 4u;
            std::memcpy(&Pixels[LeftDst], &Pixels[LeftSrc], 4u);

            const size_t RightSrc = ((size_t)(DstY + Y) * (size_t)AtlasW + (size_t)(DstX + CurW - 1)) * 4u;
            const size_t RightDst = ((size_t)(DstY + Y) * (size_t)AtlasW + (size_t)(DstX + CurW)) * 4u;
            std::memcpy(&Pixels[RightDst], &Pixels[RightSrc], 4u);
        }

        for (int X = -1; X <= CurW; ++X)
        {
            const size_t TopSrc = ((size_t)DstY * (size_t)AtlasW + (size_t)(DstX + X)) * 4u;
            const size_t TopDst = ((size_t)(DstY - 1) * (size_t)AtlasW + (size_t)(DstX + X)) * 4u;
            std::memcpy(&Pixels[TopDst], &Pixels[TopSrc], 4u);

            const size_t BottomSrc = ((size_t)(DstY + CurH - 1) * (size_t)AtlasW + (size_t)(DstX + X)) * 4u;
            const size_t BottomDst = ((size_t)(DstY + CurH) * (size_t)AtlasW + (size_t)(DstX + X)) * 4u;
            std::memcpy(&Pixels[BottomDst], &Pixels[BottomSrc], 4u);
        }
    }

    TilesetAtlas Atlas;
    Atlas.Image = UploadRgbaFull(AtlasW, AtlasH, Pixels.data());
    if (!Atlas.Image.Valid())
        return false;

    Atlas.FrameWidth = FrameW;
    Atlas.FrameHeight = FrameH;
    Atlas.CellWidth = CellW;
    Atlas.CellHeight = CellH;
    Atlas.Columns = Columns;
    Atlas.FrameCount = FrameCount;
    Atlas.MegaIdxToSlot = std::move(MegaIdxToSlot);

    mTilesets.AtlasesByName[TilesetName] = std::move(Atlas);
    const auto& StoredAtlas = mTilesets.AtlasesByName[TilesetName];
    Core::Utils::Logger::Info("ResourceCache",
        "Loaded HD tileset atlas from Tileset/" + TilesetName + ".dds.vr4: " +
        std::to_string(StoredAtlas.FrameCount) + " unique frames " +
        std::to_string(StoredAtlas.Image.Width) + "x" +
        std::to_string(StoredAtlas.Image.Height) +
        " (packed into " + std::to_string((AtlasW * AtlasH * 4) / (1024 * 1024)) + " MB texture)");
    return true;
#else
    Core::Utils::Logger::Info("ResourceCache",
        "HD tileset atlas decode is unavailable on this platform for Tileset/" +
        TilesetName + ".dds.vr4");
    return false;
#endif
}

const std::vector<GpuImage>* ResourceCache::GetTilesetImages(const std::string& TilesetName)
{
    auto It = mTilesets.ImagesByName.find(TilesetName);
    if (It != mTilesets.ImagesByName.end()) return &It->second;
    if (BuildTilesetImages(TilesetName)) return &mTilesets.ImagesByName[TilesetName];
    return nullptr;
}

const TilesetAtlas* ResourceCache::GetTilesetAtlas(const std::string& TilesetName)
{
    return GetTilesetAtlasForMap(TilesetName, nullptr);
}

const TilesetAtlas* ResourceCache::GetTilesetAtlasForMap(const std::string& TilesetName,
                                                        const std::vector<uint16_t>* MegaTileIds)
{
    auto It = mTilesets.AtlasesByName.find(TilesetName);
    if (It != mTilesets.AtlasesByName.end()) return &It->second;
    if (BuildTilesetAtlasForMap(TilesetName, MegaTileIds)) return &mTilesets.AtlasesByName[TilesetName];
    return nullptr;
}

bool ResourceCache::BuildTilesetCreepFrames(const std::string& TilesetName)
{
    auto TryRead = [&](const std::string& Base) -> std::optional<std::vector<uint8_t>>
    {
        if (auto Data = ReadFile(Base)) return Data;
        std::string Upper = Base;
        std::transform(Upper.begin(), Upper.end(), Upper.begin(),
            [](unsigned char C) { return (char)std::toupper(C); });
        if (auto Data = ReadFile(Upper)) return Data;
        std::string Lower = Base;
        std::transform(Lower.begin(), Lower.end(), Lower.begin(),
            [](unsigned char C) { return (char)std::tolower(C); });
        return ReadFile(Lower);
    };

    auto& Out = mTilesets.CreepFramesByName[TilesetName];
    Out.clear();

#ifdef _WIN32
    // Remastered archives store the original tileset creep-edge GRP as DDS
    // frames. Prefer it so edge pixels match the HD terrain exactly.
    for (const std::string& Path : {
            "Tileset/" + TilesetName + ".dds.grp",
            "Tileset/" + TilesetName + ".grp.dds"})
    {
        auto Data = TryRead(Path);
        if (!Data) continue;
        auto Frames = LoadDdsGrpRgbaFrames(*Data);
        if (!Frames || Frames->empty()) continue;
        for (const auto& Frame : *Frames)
        {
            TilesetCreepFrame Uploaded;
            Uploaded.Image = UploadRgbaFull(Frame.Width, Frame.Height, Frame.Rgba.data());
            // Remastered tileset GRPs are authored at 4x resolution but each
            // creep edge frame still occupies one logical 32x32 map tile.
            Uploaded.WorldWidth = 32;
            Uploaded.WorldHeight = 32;
            if (Uploaded.Image.Valid()) Out.push_back(std::move(Uploaded));
        }
        if (!Out.empty())
        {
            Core::Utils::Logger::Info("ResourceCache",
                "Loaded authentic HD creep edges from " + Path + ": " +
                std::to_string(Out.size()) + " frames");
            return true;
        }
    }
#endif

    auto GrpData = TryRead("Tileset/" + TilesetName + ".grp");
    auto WpeData = TryRead("Tileset/" + TilesetName + ".wpe");
    if (!GrpData || !WpeData)
        return false;
    auto Palette = DecodeWpe(*WpeData);
    if (!Palette)
        return false;
    GrpDecodeOptions Options;
    Options.PaletteOverride = &*Palette;
    Options.GenerateTeamColorMask = false;
    auto Decoded = DecodeGrpToRgba(*GrpData, *Palette, Options);
    if (!Decoded)
        return false;
    for (const auto& Frame : Decoded->Frames)
    {
        TilesetCreepFrame Uploaded;
        Uploaded.Image = UploadRgbaFull(Frame.Width, Frame.Height, Frame.Rgba.data());
        Uploaded.OffsetX = Frame.OriginX;
        Uploaded.OffsetY = Frame.OriginY;
        Uploaded.WorldWidth = Frame.Width;
        Uploaded.WorldHeight = Frame.Height;
        if (Uploaded.Image.Valid()) Out.push_back(std::move(Uploaded));
    }
    if (!Out.empty())
    {
        Core::Utils::Logger::Info("ResourceCache",
            "Loaded authentic classic creep edges from Tileset/" + TilesetName +
            ".grp: " + std::to_string(Out.size()) + " frames");
    }
    return !Out.empty();
}

const std::vector<TilesetCreepFrame>* ResourceCache::GetTilesetCreepFrames(
    const std::string& TilesetName)
{
    auto It = mTilesets.CreepFramesByName.find(TilesetName);
    if (It != mTilesets.CreepFramesByName.end()) return &It->second;
    if (BuildTilesetCreepFrames(TilesetName))
        return &mTilesets.CreepFramesByName[TilesetName];
    return nullptr;
}

} // namespace Resource::Graphics


// === Consolidated from src/Resource/AssetCacheViews.cpp ===
namespace Resource::Graphics
{

std::optional<std::vector<uint8_t>> ArchiveAssetSourceView::ReadFile(const std::string& Path) const
{
    return mCache ? mCache->ReadFile(Path) : std::nullopt;
}

std::optional<std::vector<uint8_t>> ArchiveAssetSourceView::ReadWithFallback(const std::string& Path) const
{
    return mCache ? mCache->ReadWithFallback(Path) : std::nullopt;
}

std::vector<std::string> ArchiveAssetSourceView::DebugBuildExtractedAssetCandidates(const std::string& Path) const
{
    return mCache ? mCache->DebugBuildExtractedAssetCandidates(Path) : std::vector<std::string>{};
}

Resource::GameDataLoader* DataAssetCacheView::GetDataLoader() const
{
    return mCache ? &mCache->GetDataLoader() : nullptr;
}

uint32_t SpriteAssetCacheView::ResolveImageID(int ChkUnitID) const
{
    return mCache ? mCache->ResolveImageID(ChkUnitID) : 0;
}

uint32_t SpriteAssetCacheView::ResolveConstructionImageID(int ChkUnitID) const
{
    return mCache ? mCache->ResolveConstructionImageID(ChkUnitID) : UINT32_MAX;
}

uint32_t SpriteAssetCacheView::ResolveSubunitImageID(int ChkUnitID) const
{
    return mCache ? mCache->ResolveSubunitImageID(ChkUnitID) : UINT32_MAX;
}

const std::vector<GpuImage>* SpriteAssetCacheView::GetSpriteFrames(uint32_t ImageID) const
{
    return mCache ? mCache->GetSpriteFrames(ImageID) : nullptr;
}

const std::vector<GpuImage>* SpriteAssetCacheView::GetDatImageFrames(uint32_t ImageID) const
{
    return mCache ? mCache->GetDatImageFrames(ImageID) : nullptr;
}

const std::vector<GpuImage>* SpriteAssetCacheView::GetSpriteOverlayFrames(uint32_t SpriteID) const
{
    return mCache ? mCache->GetSpriteOverlayFrames(SpriteID) : nullptr;
}

const std::vector<GpuImage>* SpriteAssetCacheView::GetSpriteFramesForChkUnit(int ChkUnitID) const
{
    if (!mCache || ChkUnitID < 0)
        return nullptr;
    return mCache->GetDatImageFrames(mCache->ResolveImageID(ChkUnitID));
}

bool TilesetAssetCacheView::SetTilesetPalette(const std::string& TilesetName) const
{
    return mCache ? mCache->SetTilesetPalette(TilesetName) : false;
}

const std::vector<GpuImage>* TilesetAssetCacheView::GetTilesetImages(const std::string& TilesetName) const
{
    return mCache ? mCache->GetTilesetImages(TilesetName) : nullptr;
}

const TilesetAtlas* TilesetAssetCacheView::GetTilesetAtlas(const std::string& TilesetName) const
{
    return mCache ? mCache->GetTilesetAtlas(TilesetName) : nullptr;
}

const TilesetAtlas* TilesetAssetCacheView::GetTilesetAtlasForMap(
    const std::string& TilesetName, const std::vector<uint16_t>* MegaTileIds) const
{
    return mCache ? mCache->GetTilesetAtlasForMap(TilesetName, MegaTileIds) : nullptr;
}

const std::vector<TilesetCreepFrame>* TilesetAssetCacheView::GetTilesetCreepFrames(
    const std::string& TilesetName) const
{
    return mCache ? mCache->GetTilesetCreepFrames(TilesetName) : nullptr;
}

const TilesetData* TilesetAssetCacheView::GetTilesetData(const std::string& TilesetName) const
{
    return mCache ? mCache->GetTilesetData(TilesetName) : nullptr;
}

const std::vector<GpuImage>* UIAssetCacheView::GetUIIcons() const
{
    return mCache ? mCache->GetUIIcons() : nullptr;
}

const std::vector<std::string>* UIAssetCacheView::GetUIIconUris() const
{
    return mCache ? mCache->GetUIIconUris() : nullptr;
}

const std::vector<GpuImage>* UIAssetCacheView::GetCommandIcons() const
{
    return mCache ? mCache->GetCommandIcons() : nullptr;
}

const std::vector<GpuImage>* UIAssetCacheView::GetCommandIconMasks() const
{
    return mCache ? mCache->GetCommandIconMasks() : nullptr;
}

const std::vector<std::string>* UIAssetCacheView::GetCommandIconUris() const
{
    return mCache ? mCache->GetCommandIconUris() : nullptr;
}

const std::vector<GpuImage>* UIAssetCacheView::GetCommandButtonSkins(CommandButtonSkinSet SkinSet) const
{
    return mCache ? mCache->GetCommandButtonSkins(SkinSet) : nullptr;
}

const std::vector<GpuImage>* UIAssetCacheView::GetWireframeFrames() const
{
    return mCache ? mCache->GetWireframeFrames() : nullptr;
}

bool UIAssetCacheView::EnsureWireframeFrame(int FrameIndex) const
{
    return mCache ? mCache->EnsureWireframeFrame(FrameIndex) : false;
}

const std::vector<GpuImage>* UIAssetCacheView::GetCursorFrames(const std::string& CursorName) const
{
    return mCache ? mCache->GetCursorFrames(CursorName) : nullptr;
}

const ImageOpaqueBounds* UIAssetCacheView::GetWireframeOpaqueBounds(int FrameIndex) const
{
    return mCache ? mCache->GetWireframeOpaqueBounds(FrameIndex) : nullptr;
}

const PortraitClip* UIAssetCacheView::GetPortraitClip(const std::string& PortraitKey) const
{
    return mCache ? mCache->GetPortraitClip(PortraitKey) : nullptr;
}

const PortraitFrame* UIAssetCacheView::GetPortraitFrame(const std::string& PortraitKey, double TimeSeconds) const
{
    return mCache ? mCache->GetPortraitFrame(PortraitKey, TimeSeconds) : nullptr;
}

GpuImage UIAssetCacheView::GetHudAggIcons() const
{
    return mCache ? mCache->GetHudAggIcons() : GpuImage{};
}

GpuImage UIAssetCacheView::GetHudTwireStrip() const
{
    return mCache ? mCache->GetHudTwireStrip() : GpuImage{};
}

GpuImage UIAssetCacheView::GetHudTunitStrip() const
{
    return mCache ? mCache->GetHudTunitStrip() : GpuImage{};
}

GpuImage UIAssetCacheView::GetHudHpBarEmptyStrip() const
{
    return mCache ? mCache->GetHudHpBarEmptyStrip() : GpuImage{};
}

GpuImage UIAssetCacheView::GetHudHpBarStrip() const
{
    return mCache ? mCache->GetHudHpBarStrip() : GpuImage{};
}

GpuImage UIAssetCacheView::GetUIConsoleLeft() const
{
    return mCache ? mCache->GetUIConsoleLeft() : GpuImage{};
}

GpuImage UIAssetCacheView::GetUIConsoleCenter() const
{
    return mCache ? mCache->GetUIConsoleCenter() : GpuImage{};
}

GpuImage UIAssetCacheView::GetUIConsoleRight() const
{
    return mCache ? mCache->GetUIConsoleRight() : GpuImage{};
}

GpuImage UIAssetCacheView::GetUIConsolePortraitFrame() const
{
    return mCache ? mCache->GetUIConsolePortraitFrame() : GpuImage{};
}

GpuImage UIAssetCacheView::GetUIConsoleApm() const
{
    return mCache ? mCache->GetUIConsoleApm() : GpuImage{};
}

GpuImage UIAssetCacheView::GetUIConsoleTime() const
{
    return mCache ? mCache->GetUIConsoleTime() : GpuImage{};
}

GpuImage UIAssetCacheView::GetUIConsole() const
{
    return mCache ? mCache->GetUIConsole() : GpuImage{};
}

GpuImage UIAssetCacheView::GetUIConsoleOver() const
{
    return mCache ? mCache->GetUIConsoleOver() : GpuImage{};
}

const PcxRegions* UIAssetCacheView::GetUIRegions() const
{
    return mCache ? &mCache->GetUIRegions() : nullptr;
}

} // namespace Resource::Graphics

// === Consolidated from src/Resource/ResourceCacheUI.cpp ===
#include "Resource/ResourceCache.h"

#include "Core/Utils/Logger.h"
#include "Resource/GrpToGpu.h"
#include "Resource/Palette.h"
#include "Resource/RawColor.h"
#include "Resource/RgbaImageFileDecoder.h"
#include "Resource/UIAssetTypes.h"

namespace Resource::Graphics
{
struct DecodedPcx
{
    int Width = 0;
    int Height = 0;
    std::vector<uint8_t> Rgba;
    PcxRegions Regions;
};

std::optional<DecodedPcx> DecodePcxToRgba(const std::vector<uint8_t>& Data);
ImageOpaqueBounds ComputeOpaqueBounds(const uint8_t* Rgba, int Width, int Height);
std::optional<ColorPalette> LoadPcxPalette(const std::vector<uint8_t>& Data);
void ApplyTransparentBlack(std::vector<uint8_t>& Rgba);
void ConvertOpaquePixelsToWhiteMask(std::vector<uint8_t>& Rgba);
std::optional<DecodedRgbaImage> ResizeRgbaImage(const DecodedRgbaImage& Source,
                                                int TargetWidth,
                                                int TargetHeight);
void AlphaBlendInto(std::vector<uint8_t>& Dest,
                    int DestWidth,
                    int DestHeight,
                    const DecodedRgbaImage& Source,
                    int DestX,
                    int DestY);
} // namespace Resource::Graphics

#include "Core/Utils/Logger.h"




namespace Resource::Graphics
{


bool ResourceCache::LoadUIGraphics()
{
    std::optional<ColorPalette> UiPalette;
    if (auto TIcon = ReadUIFileWithUpperFallback("sprites\\cmdbtns\\ticon.pcx"))
        UiPalette = LoadPcxPalette(*TIcon);

    const ColorPalette* UiPalettePtr = UiPalette ? &*UiPalette : nullptr;

    GrpDecodeOptions CommandButtonOptions = {};
    CommandButtonOptions.PaletteOverride = UiPalettePtr;
    CommandButtonOptions.TransparentIndexZero = true;

    GrpDecodeOptions CursorOptions = CommandButtonOptions;
    CursorOptions.TransparentBlack = true;
    CursorOptions.TransparentRgb = RawColor(0, 0, 0, 0);

    LoadUIIconGraphics();
    LoadCommandButtonGraphics(CommandButtonOptions);
    LoadWireframeGraphics();
    LoadHudChromeGraphics();
    LoadBlinkGraphics();

    if (mSources.UseExtractedAssets)
        LoadHdConsoleGraphics();
    else
        LoadClassicConsoleGraphics();

    LoadCursorGraphics(CursorOptions);

    Core::Utils::Logger::Info("ResourceCache",
        "UI graphics: " + std::to_string(mUI.Icons.size()) + " icons, " +
        std::to_string(mUI.CommandIcons.size()) + " command icons, " +
        std::to_string(mUI.CommandButtonSkins[static_cast<size_t>(CommandButtonSkinSet::Terran)].size()) +
        " terran button skins, " +
        std::to_string(mUI.CursorFrames.size()) + " cursors" +
        (mUI.Blink.Valid() ? ", blink" : "") +
        (mUI.Console.Valid() ? ", console" : "") +
        (mUI.ConsoleOver.Valid() ? ", conover" : ""));
    return true;
}

} // namespace Resource::Graphics

































// === Consolidated from src/Resource/ResourceCacheUIConsole.cpp ===
#include "Resource/ResourceCache.h"

#include "Core/Utils/Logger.h"

#include "Resource/RgbaImageFileDecoder.h"

#include <algorithm>
#include <filesystem>
#include <vector>

namespace Resource::Graphics
{

namespace
{
    constexpr int kConsoleCompositeTargetWidth = 640;
    constexpr int kConsoleCompositeLeftWidth = 176;
    constexpr int kConsoleCompositeRightWidth = 216;
}

bool ResourceCache::LoadNamedConsoleDdsForRace(const std::string& RaceName,
                                              const std::string& FileName,
                                              GpuImage& Out,
                                              bool TransparentBlack)
{
    if (!mSources.UseExtractedAssets)
        return false;
    return LoadWicUIImage(std::string("game/consoles/") + RaceName + "/" + FileName, Out, TransparentBlack);
}

bool ResourceCache::LoadNamedConsoleDds(const std::string& FileName,
                                        GpuImage& Out,
                                        bool TransparentBlack)
{
    return LoadNamedConsoleDdsForRace("terran", FileName, Out, TransparentBlack);
}

bool ResourceCache::BuildConsoleCompositeFromDds(GpuImage& Out)
{
#ifdef _WIN32
    auto LeftPath = FindExtractedUIAssetPath("game/consoles/terran/console_left.dds");
    auto CenterPath = FindExtractedUIAssetPath("game/consoles/terran/console_center.dds");
    auto RightPath = FindExtractedUIAssetPath("game/consoles/terran/console_right.dds");
    if (!LeftPath || !CenterPath || !RightPath)
        return false;

    auto LeftImage = LoadWicRgbaImageFromFile(*LeftPath);
    auto CenterImage = LoadWicRgbaImageFromFile(*CenterPath);
    auto RightImage = LoadWicRgbaImageFromFile(*RightPath);
    if (!LeftImage || !CenterImage || !RightImage)
        return false;

    constexpr int TargetWidth = kConsoleCompositeTargetWidth;
    constexpr int TargetLeftWidth = kConsoleCompositeLeftWidth;
    constexpr int TargetRightWidth = kConsoleCompositeRightWidth;
    constexpr int TargetCenterWidth = TargetWidth - TargetLeftWidth - TargetRightWidth;
    const int TargetHeight = std::max({LeftImage->Height, CenterImage->Height, RightImage->Height});
    if (TargetCenterWidth <= 0 || TargetHeight <= 0)
        return false;

    auto LeftScaled = ResizeRgbaImage(*LeftImage, TargetLeftWidth, TargetHeight);
    auto CenterScaled = ResizeRgbaImage(*CenterImage, TargetCenterWidth, TargetHeight);
    auto RightScaled = ResizeRgbaImage(*RightImage, TargetRightWidth, TargetHeight);
    if (!LeftScaled || !CenterScaled || !RightScaled)
        return false;

    std::vector<uint8_t> Pixels((size_t)TargetWidth * (size_t)TargetHeight * 4u, 0);
    AlphaBlendInto(Pixels, TargetWidth, TargetHeight, *LeftScaled, 0, 0);
    AlphaBlendInto(Pixels, TargetWidth, TargetHeight, *CenterScaled, TargetLeftWidth, 0);
    AlphaBlendInto(Pixels, TargetWidth, TargetHeight, *RightScaled, TargetLeftWidth + TargetCenterWidth, 0);

    Out = UploadRgbaFull(TargetWidth, TargetHeight, Pixels.data());
    return Out.Valid();
#else
    (void)Out;
    return false;
#endif
}

bool ResourceCache::LoadHdConsoleGraphics()
{
    if (!mSources.UseExtractedAssets)
        return false;

    LoadNamedConsoleDds("console_left.DDS", mUI.ConsoleLeft);
    LoadNamedConsoleDds("console_center.DDS", mUI.ConsoleCenter);
    LoadNamedConsoleDds("console_right.DDS", mUI.ConsoleRight);
    LoadNamedConsoleDds("conover.DDS", mUI.ConsolePortraitFrame);
    LoadNamedConsoleDds("console_APM.DDS", mUI.ConsoleApm);
    LoadNamedConsoleDds("console_TIME.DDS", mUI.ConsoleTime);

    if (BuildConsoleCompositeFromDds(mUI.Console))
    {
        Core::Utils::Logger::Info("ResourceCache",
            "Built terran HUD console composite from extracted DDS slices");
        return true;
    }
    return mUI.ConsoleLeft.Valid() || mUI.ConsoleCenter.Valid() || mUI.ConsoleRight.Valid();
}

bool ResourceCache::LoadHdConsoleGraphicsForRace(const std::string& RaceName)
{
    if (!mSources.UseExtractedAssets)
        return false;

    LoadNamedConsoleDdsForRace(RaceName, "console_left.DDS", mUI.ConsoleLeft);
    LoadNamedConsoleDdsForRace(RaceName, "console_center.DDS", mUI.ConsoleCenter);
    LoadNamedConsoleDdsForRace(RaceName, "console_right.DDS", mUI.ConsoleRight);
    LoadNamedConsoleDdsForRace(RaceName, "conover.DDS", mUI.ConsolePortraitFrame);
    LoadNamedConsoleDdsForRace(RaceName, "console_APM.DDS", mUI.ConsoleApm);
    LoadNamedConsoleDdsForRace(RaceName, "console_TIME.DDS", mUI.ConsoleTime);

    return mUI.ConsoleLeft.Valid() || mUI.ConsoleCenter.Valid() || mUI.ConsoleRight.Valid();
}

bool ResourceCache::LoadClassicConsoleGraphics()
{
    bool LoadedAny = false;

    auto ConsoleData = ReadUIFileWithUpperFallback("game\\nconsole.pcx");
    if (ConsoleData)
    {
        auto PcxOpt = DecodePcxToRgba(*ConsoleData);
        if (PcxOpt)
        {
            mUI.Console = UploadRgbaFull(PcxOpt->Width, PcxOpt->Height, PcxOpt->Rgba.data());
            mUI.Regions = PcxOpt->Regions;
            LoadedAny = LoadedAny || mUI.Console.Valid();
        }
    }

    auto ConsoleOverData = ReadUIFileWithUpperFallback("game\\tconsole.pcx");
    if (ConsoleOverData)
    {
        auto PcxOpt = DecodePcxToRgba(*ConsoleOverData);
        if (PcxOpt)
        {
            mUI.ConsoleOver = UploadRgbaFull(PcxOpt->Width, PcxOpt->Height, PcxOpt->Rgba.data());
            LoadedAny = LoadedAny || mUI.ConsoleOver.Valid();
        }
    }

    return LoadedAny;
}

bool ResourceCache::LoadHudChromeGraphics()
{
    bool LoadedAny = false;
    LoadedAny |= LoadPcxUIImage("game\\aggicons.pcx", mUI.HudAggIcons, true);

    // The extracted asset set has clear HD replacements for the console shell and HP bar,
    // but not for the old twire/tunit decoration strips. In extracted-assets mode those
    // classic strips produce obvious distortion when stretched into the new FHD layout,
    // so prefer the HUD's rectangle fallback until we identify a true sliced replacement.
    if (!mSources.UseExtractedAssets)
    {
        LoadedAny |= LoadPcxUIImage("game\\twire.pcx", mUI.HudTwireStrip, true);
        LoadedAny |= LoadPcxUIImage("game\\tunit.pcx", mUI.HudTunitStrip, true);
    }

    bool LoadedHdHpBar = false;
    if (mSources.UseExtractedAssets)
    {
        const bool LoadedEmpty = LoadNamedConsoleDds("pbrempt.DDS", mUI.HudHpBarEmptyStrip, true);
        const bool LoadedFull = LoadNamedConsoleDds("pbrfull.DDS", mUI.HudHpBarStrip, true);
        LoadedHdHpBar = LoadedEmpty && LoadedFull;
        if (LoadedHdHpBar)
        {
            Core::Utils::Logger::Info("ResourceCache",
                "Loaded HD HP bar strips from game/consoles/terran/pbrempt.DDS and pbrfull.DDS");
        }
        else
        {
            Core::Utils::Logger::Info("ResourceCache",
                "HD HP bar DDS availability: empty=" + std::to_string((int)LoadedEmpty) +
                ", full=" + std::to_string((int)LoadedFull) + "; falling back to thpbar.pcx");
        }
    }
    if (!LoadedHdHpBar)
        LoadedAny |= LoadPcxUIImage("game\\thpbar.pcx", mUI.HudHpBarStrip, true);
    else
        LoadedAny = true;

    return LoadedAny;
}

} // namespace Resource::Graphics


// === Consolidated from src/Resource/ResourceCacheUIAssets.cpp ===
#include "Resource/ResourceCache.h"

#include "Core/Utils/Logger.h"
#include "Resource/GrpToGpu.h"

#include <utility>

namespace Resource::Graphics
{

bool ResourceCache::LoadCommandButtonGraphics(const GrpDecodeOptions& CommandButtonOptions)
{
    bool LoadedAny = false;
    LoadedAny |= LoadPreferredUIFrames("sprites\\cmdbtns\\cmdicons.grp", mUI.CommandIcons, CommandButtonOptions, nullptr, false, "command icons");
    ExportPreferredUIFramesForGui("sprites\\cmdbtns\\cmdicons.grp", mUI.CommandIconUris, CommandButtonOptions, false, "command icons");
    LoadedAny |= LoadPreferredUIFrames("sprites\\cmdbtns\\cmdicons.grp", mUI.CommandIconMasks, CommandButtonOptions, nullptr, true, "command icon masks");
    LoadedAny |= LoadPreferredUIFrames("sprites\\cmdbtns\\tcmdbtns.grp",
                                      mUI.CommandButtonSkins[static_cast<size_t>(CommandButtonSkinSet::Terran)],
                                      CommandButtonOptions,
                                      nullptr,
                                      false,
                                      "terran command button skins");
    LoadedAny |= LoadPreferredUIFrames("sprites\\cmdbtns\\pcmdbtns.grp",
                                      mUI.CommandButtonSkins[static_cast<size_t>(CommandButtonSkinSet::Protoss)],
                                      CommandButtonOptions,
                                      nullptr,
                                      false,
                                      "protoss command button skins");
    LoadedAny |= LoadPreferredUIFrames("sprites\\cmdbtns\\zcmdbtns.grp",
                                      mUI.CommandButtonSkins[static_cast<size_t>(CommandButtonSkinSet::Zerg)],
                                      CommandButtonOptions,
                                      nullptr,
                                      false,
                                      "zerg command button skins");
    return LoadedAny;
}

bool ResourceCache::LoadUIIconGraphics()
{
    ExportPreferredUIFramesForGui("game\\icons.grp", mUI.IconUris, {}, false, "UI icons");

    bool LoadedHdIcons = false;
    if (mSources.UseExtractedAssets)
    {
        LoadedHdIcons = LoadDdsGrpUIFrames("game\\icons.dds.grp", mUI.Icons);
        if (LoadedHdIcons)
        {
            Core::Utils::Logger::Info("ResourceCache",
                "Loaded HD UI icons from game/icons.dds.grp: " + std::to_string(mUI.Icons.size()) + " frames");
        }
    }
    if (!LoadedHdIcons)
        return LoadUIGrpFrames("game\\icons.grp", mUI.Icons);
    return true;
}

bool ResourceCache::LoadWireframeGraphics()
{
#ifdef _WIN32
    if (mSources.UseExtractedAssets)
    {
        const std::string DdsPath = GetDdsGrpLogicalPath("unit\\wirefram\\wirefram.grp");
        auto DdsGrpData = ReadUIFileWithUpperFallback(DdsPath);
        if (DdsGrpData)
        {
            auto Parsed = ParseDdsGrp(*DdsGrpData);
            if (Parsed && !Parsed->Frames.empty())
            {
                mUI.WireframeFrames.resize(Parsed->Frames.size());
                mUI.WireframeOpaqueBounds.resize(Parsed->Frames.size());
                Core::Utils::Logger::Info("ResourceCache",
                    "Initialized lazy HD wireframe slots: " +
                    std::to_string(mUI.WireframeFrames.size()) + " frames");
                return true;
            }
        }
    }
#endif
    return LoadPreferredUIFrames("unit\\wirefram\\wirefram.grp",
                                 mUI.WireframeFrames,
                                 {},
                                 &mUI.WireframeOpaqueBounds,
                                 false,
                                 "wireframe frames");
}

bool ResourceCache::EnsureWireframeFrame(int FrameIndex)
{
    if (FrameIndex < 0 || FrameIndex >= static_cast<int>(mUI.WireframeFrames.size()))
        return false;

    if (mUI.WireframeFrames[static_cast<size_t>(FrameIndex)].Valid())
        return true;

#ifdef _WIN32
    if (mSources.UseExtractedAssets)
    {
        const std::string DdsPath = GetDdsGrpLogicalPath("unit\\wirefram\\wirefram.grp");
        auto DdsGrpData = ReadUIFileWithUpperFallback(DdsPath);
        if (DdsGrpData)
        {
            auto Parsed = ParseDdsGrp(*DdsGrpData);
            if (Parsed && FrameIndex < static_cast<int>(Parsed->Frames.size()))
            {
                const auto& Frame = Parsed->Frames[static_cast<size_t>(FrameIndex)];
                auto ImageOpt = LoadWicRgbaImageFromMemory(
                    DdsGrpData->data() + Frame.DataOffset,
                    Frame.ByteSize);
                if (ImageOpt && ImageOpt->Width > 0 && ImageOpt->Height > 0)
                {
                    if (FrameIndex < static_cast<int>(mUI.WireframeOpaqueBounds.size()))
                    {
                        mUI.WireframeOpaqueBounds[static_cast<size_t>(FrameIndex)] =
                            ComputeOpaqueBounds(ImageOpt->Rgba.data(), ImageOpt->Width, ImageOpt->Height);
                    }
                    mUI.WireframeFrames[static_cast<size_t>(FrameIndex)] =
                        UploadRgbaFull(ImageOpt->Width, ImageOpt->Height, ImageOpt->Rgba.data());
                    return mUI.WireframeFrames[static_cast<size_t>(FrameIndex)].Valid();
                }
            }
        }
    }
#endif

    // Fallback: decode single classic GRP frame
    auto GrpData = ReadUIFileWithUpperFallback("unit\\wirefram\\wirefram.grp");
    if (!GrpData)
        return false;

    auto SpriteOpt = DecodeGrpToRgba(*GrpData, mData.ActivePalette, {});
    if (!SpriteOpt || FrameIndex >= static_cast<int>(SpriteOpt->Frames.size()))
        return false;

    const auto& Frame = SpriteOpt->Frames[static_cast<size_t>(FrameIndex)];
    if (FrameIndex < static_cast<int>(mUI.WireframeOpaqueBounds.size()))
    {
        mUI.WireframeOpaqueBounds[static_cast<size_t>(FrameIndex)] =
            ComputeOpaqueBounds(Frame.Rgba.data(), Frame.Width, Frame.Height);
    }
    mUI.WireframeFrames[static_cast<size_t>(FrameIndex)] =
        UploadRgbaFull(Frame.Width, Frame.Height, Frame.Rgba.data());
    return mUI.WireframeFrames[static_cast<size_t>(FrameIndex)].Valid();
}

bool ResourceCache::LoadBlinkGraphics()
{
    bool LoadedHdBlink = false;
    if (mSources.UseExtractedAssets)
    {
        LoadedHdBlink = LoadDdsGrpUIFirstFrame("game\\blink.dds.grp", mUI.Blink);
        if (LoadedHdBlink)
        {
            Core::Utils::Logger::Info("ResourceCache",
                "Loaded HD blink frame from game/blink.dds.grp");
        }
    }
    if (LoadedHdBlink)
        return true;

    auto BlinkData = ReadUIFileWithUpperFallback("game\\blink.grp");
    if (!BlinkData)
        return false;

    auto SpriteOpt = DecodeGrpToRgba(*BlinkData, mData.ActivePalette);
    if (!SpriteOpt || SpriteOpt->Frames.empty())
        return false;

    mUI.Blink = UploadRgbaFull(SpriteOpt->Frames[0].Width,
                               SpriteOpt->Frames[0].Height,
                               SpriteOpt->Frames[0].Rgba.data());
    return mUI.Blink.Valid();
}

bool ResourceCache::LoadCursorGraphics(const GrpDecodeOptions& CursorOptions)
{
    constexpr const char* CursorNames[] =
    {
        "arrow", "drag", "illegal", "targn", "targg", "targr", "targy",
        "scrollu", "scrolld", "scrolll", "scrollr",
        "scrollul", "scrollur", "scrolldl", "scrolldr",
        "magg", "magr", "magy", "time"
    };

    bool LoadedAny = false;
    for (const char* CursorName : CursorNames)
    {
        std::vector<GpuImage> Frames;
        if (LoadUIGrpFrames(std::string("cursor\\") + CursorName + ".grp", Frames, CursorOptions, nullptr, false))
        {
            mUI.CursorFrames.emplace(CursorName, std::move(Frames));
            LoadedAny = true;
        }
    }
    return LoadedAny;
}

} // namespace Resource::Graphics


// === Consolidated from src/Resource/ResourceCacheUIFrames.cpp ===
#include "Resource/ResourceCache.h"

#include "Core/Utils/Logger.h"

#include "Resource/RgbaImageFileDecoder.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <objbase.h>
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#ifdef DrawText
#undef DrawText
#endif
#ifdef PlaySound
#undef PlaySound
#endif
#endif

namespace Resource::Graphics
{

namespace
{
    std::string MakeGuiExportStem(const std::string& Path, bool Hd, bool AsMask)
    {
        std::string Stem = ToLowerCopy(NormalizeLogicalPath(Path));
        for (char& C : Stem)
        {
            if (!std::isalnum((unsigned char)C))
                C = '_';
        }
        return Stem + (Hd ? "_hd" : "_classic") + (AsMask ? "_mask" : "");
    }

#ifdef _WIN32
    std::filesystem::path GetGuiExportDirectory()
    {
        return std::filesystem::temp_directory_path() / "StarCraftGuiAssetCache";
    }

    bool WritePngRgba(const std::filesystem::path& Path, const DecodedRgbaImage& Image)
    {
        using Microsoft::WRL::ComPtr;

        if (Image.Width <= 0 || Image.Height <= 0 || Image.Rgba.empty())
            return false;

        const HRESULT InitHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const bool NeedUninitialize = SUCCEEDED(InitHr);
        if (FAILED(InitHr) && InitHr != RPC_E_CHANGED_MODE)
            return false;

        auto CleanupCom = [&]()
        {
            if (NeedUninitialize)
                CoUninitialize();
        };

        ComPtr<IWICImagingFactory> Factory;
        HRESULT Hr = CoCreateInstance(CLSID_WICImagingFactory,
                                      nullptr,
                                      CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(Factory.GetAddressOf()));
        if (FAILED(Hr))
        {
            CleanupCom();
            return false;
        }

        std::filesystem::create_directories(Path.parent_path());

        ComPtr<IWICStream> Stream;
        Hr = Factory->CreateStream(Stream.GetAddressOf());
        if (FAILED(Hr))
        {
            CleanupCom();
            return false;
        }

        Hr = Stream->InitializeFromFilename(Path.wstring().c_str(), GENERIC_WRITE);
        if (FAILED(Hr))
        {
            CleanupCom();
            return false;
        }

        ComPtr<IWICBitmapEncoder> Encoder;
        Hr = Factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, Encoder.GetAddressOf());
        if (FAILED(Hr))
        {
            CleanupCom();
            return false;
        }

        Hr = Encoder->Initialize(Stream.Get(), WICBitmapEncoderNoCache);
        if (FAILED(Hr))
        {
            CleanupCom();
            return false;
        }

        ComPtr<IWICBitmapFrameEncode> Frame;
        ComPtr<IPropertyBag2> Properties;
        Hr = Encoder->CreateNewFrame(Frame.GetAddressOf(), Properties.GetAddressOf());
        if (FAILED(Hr))
        {
            CleanupCom();
            return false;
        }

        Hr = Frame->Initialize(Properties.Get());
        if (FAILED(Hr))
        {
            CleanupCom();
            return false;
        }

        Hr = Frame->SetSize((UINT)Image.Width, (UINT)Image.Height);
        if (FAILED(Hr))
        {
            CleanupCom();
            return false;
        }

        WICPixelFormatGUID PixelFormat = GUID_WICPixelFormat32bppBGRA;
        Hr = Frame->SetPixelFormat(&PixelFormat);
        if (FAILED(Hr) || PixelFormat != GUID_WICPixelFormat32bppBGRA)
        {
            CleanupCom();
            return false;
        }

        std::vector<uint8_t> Bgra = Image.Rgba;
        for (size_t I = 0; I + 3 < Bgra.size(); I += 4)
            std::swap(Bgra[I + 0], Bgra[I + 2]);

        const UINT Stride = (UINT)Image.Width * 4u;
        Hr = Frame->WritePixels((UINT)Image.Height, Stride, (UINT)Bgra.size(), Bgra.data());
        if (FAILED(Hr))
        {
            CleanupCom();
            return false;
        }

        Hr = Frame->Commit();
        if (SUCCEEDED(Hr))
            Hr = Encoder->Commit();
        CleanupCom();
        return SUCCEEDED(Hr);
    }
#endif

} // namespace

std::optional<std::vector<uint8_t>> ResourceCache::ReadUIFileWithUpperFallback(const std::string& Path)
{
    auto Data = ReadFile(Path);
    if (Data)
        return Data;

    std::string Upper;
    Upper.reserve(Path.size());
    for (char C : Path)
        Upper += (char)std::toupper((unsigned char)C);
    return ReadFile(Upper);
}

std::optional<std::filesystem::path> ResourceCache::FindExtractedUIAssetPath(const std::string& LogicalPath) const
{
    if (!mSources.UseExtractedAssets || mSources.AssetsRoot.empty())
        return std::nullopt;

    const auto Candidates = BuildExtractedAssetCandidates(LogicalPath);
    for (const auto& Candidate : Candidates)
    {
        auto FullPath = std::filesystem::path(mSources.AssetsRoot) / std::filesystem::path(Candidate);
        if (!std::filesystem::exists(FullPath))
            continue;

        if (Candidate != NormalizeLogicalPath(LogicalPath))
        {
            Core::Utils::Logger::Info("ResourceCache",
                "Extracted asset remap: " + LogicalPath + " -> " + Candidate);
        }
        return FullPath;
    }
    return std::nullopt;
}

bool ResourceCache::LoadWicUIImage(const std::string& LogicalPath,
                                   GpuImage& Out,
                                   bool TransparentBlack)
{
#ifdef _WIN32
    auto FullPath = FindExtractedUIAssetPath(LogicalPath);
    if (!FullPath)
        return false;

    auto ImageOpt = LoadWicRgbaImageFromFile(*FullPath);
    if (!ImageOpt)
        return false;

    if (TransparentBlack)
        ApplyTransparentBlack(ImageOpt->Rgba);

    Out = UploadRgbaFull(ImageOpt->Width, ImageOpt->Height, ImageOpt->Rgba.data());
    return Out.Valid();
#else
    (void)LogicalPath;
    (void)Out;
    (void)TransparentBlack;
    return false;
#endif
}

bool ResourceCache::UploadDecodedUIFrames(const DecodedSprite& Sprite,
                                          std::vector<GpuImage>& Out,
                                          std::vector<ImageOpaqueBounds>* OutBounds,
                                          bool AsMask)
{
    Out.reserve(Sprite.FrameCount());
    if (OutBounds)
        OutBounds->reserve(Sprite.FrameCount());
    for (const auto& Frame : Sprite.Frames)
    {
        if (OutBounds)
            OutBounds->push_back(ComputeOpaqueBounds(Frame.Rgba.data(), Frame.Width, Frame.Height));
        if (!AsMask)
        {
            Out.push_back(UploadRgbaFull(Frame.Width, Frame.Height, Frame.Rgba.data()));
            continue;
        }

        std::vector<uint8_t> Mask = Frame.Rgba;
        ConvertOpaquePixelsToWhiteMask(Mask);
        Out.push_back(UploadRgbaFull(Frame.Width, Frame.Height, Mask.data()));
    }
    return !Out.empty();
}

bool ResourceCache::LoadUIGrpFrames(const std::string& Path,
                                    std::vector<GpuImage>& Out,
                                    const GrpDecodeOptions& Options,
                                    std::vector<ImageOpaqueBounds>* OutBounds,
                                    bool AsMask)
{
    auto GrpData = ReadUIFileWithUpperFallback(Path);
    if (!GrpData)
        return false;

    auto SpriteOpt = DecodeGrpToRgba(*GrpData, mData.ActivePalette, Options);
    if (!SpriteOpt)
        return false;

    return UploadDecodedUIFrames(*SpriteOpt, Out, OutBounds, AsMask);
}

bool ResourceCache::LoadDdsGrpUIFrames(const std::string& Path,
                                       std::vector<GpuImage>& Out,
                                       std::vector<ImageOpaqueBounds>* OutBounds,
                                       bool AsMask)
{
#ifdef _WIN32
    auto DdsGrpData = ReadUIFileWithUpperFallback(Path);
    if (!DdsGrpData)
        return false;

    auto Parsed = ParseDdsGrp(*DdsGrpData);
    if (!Parsed)
        return false;

    std::vector<GpuImage> Frames;
    Frames.reserve(Parsed->Frames.size());
    std::vector<ImageOpaqueBounds> Bounds;
    if (OutBounds)
        Bounds.reserve(Parsed->Frames.size());
    for (const auto& Frame : Parsed->Frames)
    {
        auto ImageOpt = LoadWicRgbaImageFromMemory(DdsGrpData->data() + Frame.DataOffset, Frame.ByteSize);
        if (!ImageOpt)
            return false;

        if (Frame.Width > 0 && Frame.Height > 0 &&
            (ImageOpt->Width != Frame.Width || ImageOpt->Height != Frame.Height))
        {
            Core::Utils::Logger::Warning("ResourceCache",
                "DDSGRP frame size mismatch in " + Path +
                ": header=" + std::to_string(Frame.Width) + "x" + std::to_string(Frame.Height) +
                ", decoded=" + std::to_string(ImageOpt->Width) + "x" + std::to_string(ImageOpt->Height));
        }

        if (OutBounds)
            Bounds.push_back(ComputeOpaqueBounds(ImageOpt->Rgba.data(), ImageOpt->Width, ImageOpt->Height));
        if (!AsMask)
        {
            Frames.push_back(UploadRgbaFull(ImageOpt->Width, ImageOpt->Height, ImageOpt->Rgba.data()));
            continue;
        }

        std::vector<uint8_t> Mask = ImageOpt->Rgba;
        ConvertOpaquePixelsToWhiteMask(Mask);
        Frames.push_back(UploadRgbaFull(ImageOpt->Width, ImageOpt->Height, Mask.data()));
    }

    if (Frames.empty())
        return false;

    Out = std::move(Frames);
    if (OutBounds)
        *OutBounds = std::move(Bounds);
    return true;
#else
    (void)Path;
    (void)Out;
    (void)OutBounds;
    (void)AsMask;
    return false;
#endif
}

std::string ResourceCache::GetDdsGrpLogicalPath(const std::string& ClassicPath) const
{
    const std::string Lower = ToLowerCopy(ClassicPath);
    if (Lower.size() >= 4 && Lower.substr(Lower.size() - 4) == ".grp")
        return ClassicPath.substr(0, ClassicPath.size() - 4) + ".dds.grp";
    return ClassicPath;
}

bool ResourceCache::LoadPreferredUIFrames(const std::string& ClassicPath,
                                          std::vector<GpuImage>& Out,
                                          const GrpDecodeOptions& Options,
                                          std::vector<ImageOpaqueBounds>* OutBounds,
                                          bool AsMask,
                                          const char* DebugLabel)
{
    if (mSources.UseExtractedAssets)
    {
        std::vector<GpuImage> HdFrames;
        std::vector<ImageOpaqueBounds> HdBounds;
        const std::string DdsPath = GetDdsGrpLogicalPath(ClassicPath);
        if (LoadDdsGrpUIFrames(DdsPath, HdFrames, OutBounds ? &HdBounds : nullptr, AsMask))
        {
            if (DebugLabel)
            {
                Core::Utils::Logger::Info("ResourceCache",
                    std::string("Loaded HD ") + DebugLabel + " from " + DdsPath +
                    ": " + std::to_string(HdFrames.size()) + " frames");
            }
            Out = std::move(HdFrames);
            if (OutBounds)
                *OutBounds = std::move(HdBounds);
            return true;
        }
    }
    return LoadUIGrpFrames(ClassicPath, Out, Options, OutBounds, AsMask);
}

bool ResourceCache::ExportPreferredUIFramesForGui(const std::string& ClassicPath,
                                                  std::vector<std::string>& OutUris,
                                                  const GrpDecodeOptions& Options,
                                                  bool AsMask,
                                                  const char* DebugLabel)
{
#ifdef _WIN32
    const std::filesystem::path ExportDir = GetGuiExportDirectory();
    const bool CanUseHd = mSources.UseExtractedAssets;
    const std::string Stem = MakeGuiExportStem(ClassicPath, CanUseHd, AsMask);

    // Fast path: if exported PNGs already exist in temp cache, skip expensive decoding and encoding
    char TestName[128];
    std::snprintf(TestName, sizeof(TestName), "%s_%03zu.png", Stem.c_str(), (size_t)0);
    if (std::filesystem::exists(ExportDir / TestName))
    {
        std::vector<std::string> CachedUris;
        size_t Idx = 0;
        while (true)
        {
            char CurName[128];
            std::snprintf(CurName, sizeof(CurName), "%s_%03zu.png", Stem.c_str(), Idx);
            const std::filesystem::path CurPath = ExportDir / CurName;
            if (!std::filesystem::exists(CurPath))
                break;
            std::string Uri = CurPath.string();
            std::replace(Uri.begin(), Uri.end(), '\\', '/');
            CachedUris.push_back(std::move(Uri));
            ++Idx;
        }
        if (!CachedUris.empty())
        {
            OutUris = std::move(CachedUris);
            if (DebugLabel)
            {
                Core::Utils::Logger::Info("ResourceCache",
                    std::string("Using cached GUI ") + DebugLabel + " PNGs: " +
                    std::to_string(OutUris.size()) + " frames");
            }
            return true;
        }
    }

    std::vector<DecodedRgbaImage> Frames;
    bool LoadedHd = false;
    if (mSources.UseExtractedAssets)
    {
        const std::string DdsPath = GetDdsGrpLogicalPath(ClassicPath);
        auto DdsGrpData = ReadUIFileWithUpperFallback(DdsPath);
        if (DdsGrpData)
        {
            auto Parsed = ParseDdsGrp(*DdsGrpData);
            if (Parsed)
            {
                Frames.reserve(Parsed->Frames.size());
                for (const auto& Frame : Parsed->Frames)
                {
                    auto ImageOpt = LoadWicRgbaImageFromMemory(
                        DdsGrpData->data() + Frame.DataOffset,
                        Frame.ByteSize);
                    if (!ImageOpt)
                    {
                        Frames.clear();
                        break;
                    }
                    Frames.push_back(std::move(*ImageOpt));
                }
                LoadedHd = !Frames.empty();
            }
        }
    }

    if (Frames.empty())
    {
        auto GrpData = ReadUIFileWithUpperFallback(ClassicPath);
        if (!GrpData)
            return false;

        auto SpriteOpt = DecodeGrpToRgba(*GrpData, mData.ActivePalette, Options);
        if (!SpriteOpt)
            return false;

        Frames.reserve(SpriteOpt->Frames.size());
        for (const auto& Frame : SpriteOpt->Frames)
        {
            DecodedRgbaImage Image = {};
            Image.Width = Frame.Width;
            Image.Height = Frame.Height;
            Image.Rgba = Frame.Rgba;
            Frames.push_back(std::move(Image));
        }
    }

    if (Frames.empty())
        return false;

    std::vector<std::string> Uris;
    Uris.reserve(Frames.size());
    for (size_t I = 0; I < Frames.size(); ++I)
    {
        DecodedRgbaImage Image = std::move(Frames[I]);
        if (AsMask)
            ConvertOpaquePixelsToWhiteMask(Image.Rgba);

        char Name[128];
        std::snprintf(Name, sizeof(Name), "%s_%03zu.png", Stem.c_str(), I);
        const std::filesystem::path OutPath = ExportDir / Name;
        if (!std::filesystem::exists(OutPath) && !WritePngRgba(OutPath, Image))
            return false;

        std::string Uri = OutPath.string();
        std::replace(Uri.begin(), Uri.end(), '\\', '/');
        Uris.push_back(std::move(Uri));
    }

    OutUris = std::move(Uris);
    if (DebugLabel)
    {
        Core::Utils::Logger::Info("ResourceCache",
            std::string("Exported GUI ") + DebugLabel + " PNGs: " +
            std::to_string(OutUris.size()) + " frames");
    }
    return true;
#else
    (void)ClassicPath;
    (void)OutUris;
    (void)Options;
    (void)AsMask;
    (void)DebugLabel;
    return false;
#endif
}

bool ResourceCache::LoadDdsGrpUIFirstFrame(const std::string& Path, GpuImage& Out)
{
#ifdef _WIN32
    auto DdsGrpData = ReadUIFileWithUpperFallback(Path);
    if (!DdsGrpData)
        return false;

    auto Parsed = ParseDdsGrp(*DdsGrpData);
    if (!Parsed || Parsed->Frames.empty())
        return false;

    const auto& Frame = Parsed->Frames.front();
    auto ImageOpt = LoadWicRgbaImageFromMemory(DdsGrpData->data() + Frame.DataOffset, Frame.ByteSize);
    if (!ImageOpt)
        return false;

    Out = UploadRgbaFull(ImageOpt->Width, ImageOpt->Height, ImageOpt->Rgba.data());
    return Out.Valid();
#else
    (void)Path;
    (void)Out;
    return false;
#endif
}

bool ResourceCache::LoadPcxUIImage(const std::string& Path,
                                   GpuImage& Out,
                                   bool TransparentBlack)
{
    auto PcxData = ReadUIFileWithUpperFallback(Path);
    if (!PcxData)
        return false;

    auto PcxOpt = DecodePcxToRgba(*PcxData);
    if (!PcxOpt)
        return false;

    if (TransparentBlack)
        ApplyTransparentBlack(PcxOpt->Rgba);

    Out = UploadRgbaFull(PcxOpt->Width, PcxOpt->Height, PcxOpt->Rgba.data());
    return Out.Valid();
}

} // namespace Resource::Graphics

// === Consolidated PcxToGpu ===
#include "Core/Utils/Logger.h"

#include <algorithm>
#include <cstring>
#include <cstdint>

namespace Resource::Graphics
{

#pragma pack(push, 1)
struct PcxHeader
{
    uint8_t  Manufacturer;
    uint8_t  Version;
    uint8_t  Encoding;
    uint8_t  BitsPerPixel;
    uint16_t XMin, YMin, XMax, YMax;
    uint16_t HDpi, VDpi;
    uint8_t  ColorMap[48];
    uint8_t  Reserved;
    uint8_t  ColorPlanes;
    uint16_t BytesPerLine;
    uint16_t PaletteType;
    uint16_t HScrSize, VScrSize;
    uint8_t  Padding[54];
};
#pragma pack(pop)

static bool IsRed(uint8_t R, uint8_t G, uint8_t B)
{
    return R >= 220 && G < 40 && B < 40;
}

std::optional<DecodedPcx> DecodePcxToRgba(const std::vector<uint8_t>& Data)
{
    if (Data.size() < sizeof(PcxHeader) + 256 * 3 + 1) return std::nullopt;

    const PcxHeader* Hdr = reinterpret_cast<const PcxHeader*>(Data.data());
    if (Hdr->Manufacturer != 10 || Hdr->Version < 5) return std::nullopt;

    int Width = Hdr->XMax - Hdr->XMin + 1;
    int Height = Hdr->YMax - Hdr->YMin + 1;
    int Bpl = Hdr->BytesPerLine;
    if (Bpl == 0 || Bpl < Width) Bpl = Width;

    if (Width <= 0 || Height <= 0 || Width > 4096 || Height > 4096) return std::nullopt;

    std::vector<uint8_t> Pixels(Width * Height, 0);
    size_t SrcPos = sizeof(PcxHeader);
    int Planes = Hdr->ColorPlanes ? Hdr->ColorPlanes : 1;

    for (int Y = 0; Y < Height; Y++)
    {
        for (int P = 0; P < Planes; P++)
        {
            int X = 0;
            int RowOff = Y * Width;
            while (X < Bpl && SrcPos < Data.size())
            {
                uint8_t Byte = Data[SrcPos++];
                if ((Byte & 0xC0) == 0xC0)
                {
                    int Count = Byte & 0x3F;
                    uint8_t Value = (SrcPos < Data.size()) ? Data[SrcPos++] : 0;
                    for (int R = 0; R < Count && X < Bpl; R++, X++)
                    {
                        if (X < Width && RowOff + X < Width * Height)
                            Pixels[RowOff + X] = Value;
                    }
                }
                else if (X < Width && RowOff + X < Width * Height)
                {
                    Pixels[RowOff + X] = Byte;
                    X++;
                }
                else X++;
            }
        }
    }

    const uint8_t* PalStart = nullptr;
    for (size_t I = Data.size() - 256 * 3 - 1; I < Data.size(); I++)
    {
        if (Data[I] == 0x0C && I + 1 + 256 * 3 <= Data.size())
        {
            PalStart = &Data[I + 1];
            break;
        }
    }
    if (!PalStart) return std::nullopt;

    int RedIdx = -1;
    for (int I = 1; I < 256; I++)
    {
        if (IsRed(PalStart[I * 3 + 0], PalStart[I * 3 + 1], PalStart[I * 3 + 2]))
        { RedIdx = I; break; }
    }

    std::vector<bool> IsRedPixel(Width * Height, false);
    for (int I = 0; I < Width * Height; I++)
    {
        uint8_t Idx = Pixels[I];
        IsRedPixel[I] = (Idx == RedIdx) || (RedIdx == -1 && Idx != 0 &&
            IsRed(PalStart[Idx * 3 + 0], PalStart[Idx * 3 + 1], PalStart[Idx * 3 + 2]));
    }

    struct Rect { int X1 = 9999, Y1 = 9999, X2 = 0, Y2 = 0; bool Active = false; };
    std::vector<Rect> Rects;
    for (int Y = 0; Y < Height; Y++)
    {
        for (int X = 0; X < Width; X++)
        {
            if (IsRedPixel[Y * Width + X])
            {
                int RX1 = X, RY1 = Y, RX2 = X, RY2 = Y;
                while (RX2 + 1 < Width && IsRedPixel[Y * Width + RX2 + 1]) RX2++;
                for (int DY = RY1; DY <= RY2 + 8 && DY < Height; DY++)
                {
                    int RowRed = 0;
                    for (int DX = RX1; DX <= RX2; DX++)
                        if (DY < Height && IsRedPixel[DY * Width + DX]) RowRed++;
                    if (RowRed > (RX2 - RX1 + 1) * 3 / 4) { RY2 = DY; }
                    else break;
                }
                int RW = RX2 - RX1 + 1, RH = RY2 - RY1 + 1;
                if (RW >= 10 && RH >= 10)
                {
                    bool Merged = false;
                    for (auto& R : Rects)
                    {
                        int Ox = std::max(RX1, R.X1), Oy = std::max(RY1, R.Y1);
                        int Ox2 = std::min(RX2, R.X2), Oy2 = std::min(RY2, R.Y2);
                        if (Ox <= Ox2 && Oy <= Oy2 && (RW * RH < 2000 || R.Active))
                        {
                            R.X1 = std::min(R.X1, RX1); R.Y1 = std::min(R.Y1, RY1);
                            R.X2 = std::max(R.X2, RX2); R.Y2 = std::max(R.Y2, RY2);
                            R.Active = true;
                            Merged = true;
                            break;
                        }
                    }
                    if (!Merged)
                    {
                        Rect NR;
                        NR.X1 = RX1; NR.Y1 = RY1; NR.X2 = RX2; NR.Y2 = RY2;
                        NR.Active = true;
                        Rects.push_back(NR);
                    }
                }
                X = RX2;
            }
        }
    }

    std::sort(Rects.begin(), Rects.end(), [](const Rect& A, const Rect& B) {
        return A.Y1 < B.Y1 || (A.Y1 == B.Y1 && A.X1 < B.X1);
    });

    DecodedPcx Out;
    Out.Width = Width;
    Out.Height = Height;

    for (auto& R : Rects)
    {
        int RW = R.X2 - R.X1 + 1, RH = R.Y2 - R.Y1 + 1;
        if (RW < 10 || RH < 10) continue;
        PcxRegion PR;
        PR.X = R.X1; PR.Y = R.Y1; PR.W = RW; PR.H = RH;
        bool InBottom = (R.Y1 >= Height - 160);
        bool IsRight = (R.X1 >= Width * 2 / 3);
        if (InBottom)
        {
            if (RW < 60 && RH < 60)
                Out.Regions.CmdButtons.push_back(PR);
            else if (IsRight)
                Out.Regions.CmdButtons.push_back(PR);
            else if (RW > 80 && RH > 80)
                Out.Regions.Portrait.push_back(PR);
            else if (RW > 80)
                Out.Regions.Info.push_back(PR);
            else
                Out.Regions.Minimap.push_back(PR);
        }
        else
        {
            if (RW > 100)
                Out.Regions.Resources.push_back(PR);
        }
    }

    Out.Rgba.resize(static_cast<size_t>(Width) * static_cast<size_t>(Height) * 4);
    for (int I = 0; I < Width * Height; I++)
    {
        uint8_t Idx = Pixels[I];
        bool Transparent = IsRedPixel[I] || Idx == 0;
        Out.Rgba[I * 4 + 0] = PalStart[Idx * 3 + 0];
        Out.Rgba[I * 4 + 1] = PalStart[Idx * 3 + 1];
        Out.Rgba[I * 4 + 2] = PalStart[Idx * 3 + 2];
        Out.Rgba[I * 4 + 3] = Transparent ? 0 : 255;
    }

    return Out;
}

} // namespace Resource::Graphics

// === Consolidated UIAssetImageHelpers ===
#include <algorithm>
#include <cmath>
#include <cstddef>

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "third_party/stb_image_resize.h"

namespace Resource::Graphics
{

ImageOpaqueBounds ComputeOpaqueBounds(const uint8_t* Rgba, int Width, int Height)
{
    if (!Rgba || Width <= 0 || Height <= 0)
        return {};

    int MinX = Width;
    int MinY = Height;
    int MaxX = -1;
    int MaxY = -1;
    for (int Y = 0; Y < Height; ++Y)
    {
        for (int X = 0; X < Width; ++X)
        {
            const size_t Offset = ((size_t)Y * (size_t)Width + (size_t)X) * 4u;
            if (Rgba[Offset + 3] == 0)
                continue;

            MinX = std::min(MinX, X);
            MinY = std::min(MinY, Y);
            MaxX = std::max(MaxX, X);
            MaxY = std::max(MaxY, Y);
        }
    }

    if (MaxX < MinX || MaxY < MinY)
        return {};

    return { MinX, MinY, MaxX - MinX + 1, MaxY - MinY + 1 };
}

std::optional<ColorPalette> LoadPcxPalette(const std::vector<uint8_t>& Data)
{
    if (Data.size() < 769)
        return std::nullopt;

    const uint8_t* PaletteData = nullptr;
    for (size_t Offset = Data.size() - 769; Offset + 769 <= Data.size(); ++Offset)
    {
        if (Data[Offset] == 0x0C)
        {
            PaletteData = Data.data() + Offset + 1;
            break;
        }
    }
    if (!PaletteData)
        return std::nullopt;

    ColorPalette Palette = {};
    for (int I = 0; I < 256; ++I)
    {
        Palette.Colors[I].R = PaletteData[I * 3 + 0];
        Palette.Colors[I].G = PaletteData[I * 3 + 1];
        Palette.Colors[I].B = PaletteData[I * 3 + 2];
        Palette.Colors[I].A = 255;
    }
    return Palette;
}

void ApplyTransparentBlack(std::vector<uint8_t>& Rgba)
{
    for (size_t I = 0; I + 3 < Rgba.size(); I += 4)
    {
        if (Rgba[I + 3] == 0)
            continue;
        if (Rgba[I + 0] <= 8 && Rgba[I + 1] <= 8 && Rgba[I + 2] <= 8)
            Rgba[I + 3] = 0;
    }
}

void ConvertOpaquePixelsToWhiteMask(std::vector<uint8_t>& Rgba)
{
    for (size_t Pixel = 0; Pixel + 3 < Rgba.size(); Pixel += 4)
    {
        if (Rgba[Pixel + 3] == 0)
            continue;
        Rgba[Pixel + 0] = 255;
        Rgba[Pixel + 1] = 255;
        Rgba[Pixel + 2] = 255;
    }
}

std::optional<DecodedRgbaImage> ResizeRgbaImage(const DecodedRgbaImage& Source,
                                                int TargetWidth,
                                                int TargetHeight)
{
    if (Source.Width <= 0 || Source.Height <= 0 ||
        TargetWidth <= 0 || TargetHeight <= 0 ||
        Source.Rgba.empty())
    {
        return std::nullopt;
    }

    DecodedRgbaImage Resized = {};
    Resized.Width = TargetWidth;
    Resized.Height = TargetHeight;
    Resized.Rgba.resize((size_t)TargetWidth * (size_t)TargetHeight * 4u);
    if (!stbir_resize_uint8(Source.Rgba.data(),
                            Source.Width, Source.Height, 0,
                            Resized.Rgba.data(),
                            TargetWidth, TargetHeight, 0,
                            4))
    {
        return std::nullopt;
    }
    return Resized;
}

void AlphaBlendInto(std::vector<uint8_t>& Dest,
                    int DestWidth,
                    int DestHeight,
                    const DecodedRgbaImage& Source,
                    int DestX,
                    int DestY)
{
    if (DestWidth <= 0 || DestHeight <= 0 || Source.Width <= 0 || Source.Height <= 0)
        return;

    for (int Y = 0; Y < Source.Height; ++Y)
    {
        const int OutY = DestY + Y;
        if (OutY < 0 || OutY >= DestHeight)
            continue;

        for (int X = 0; X < Source.Width; ++X)
        {
            const int OutX = DestX + X;
            if (OutX < 0 || OutX >= DestWidth)
                continue;

            const size_t SrcOffset = ((size_t)Y * (size_t)Source.Width + (size_t)X) * 4u;
            const uint8_t SrcA = Source.Rgba[SrcOffset + 3];
            if (SrcA == 0)
                continue;

            const size_t DstOffset = ((size_t)OutY * (size_t)DestWidth + (size_t)OutX) * 4u;
            if (SrcA == 255)
            {
                Dest[DstOffset + 0] = Source.Rgba[SrcOffset + 0];
                Dest[DstOffset + 1] = Source.Rgba[SrcOffset + 1];
                Dest[DstOffset + 2] = Source.Rgba[SrcOffset + 2];
                Dest[DstOffset + 3] = 255;
                continue;
            }

            const float SrcAlpha = (float)SrcA / 255.0f;
            const float DstAlpha = (float)Dest[DstOffset + 3] / 255.0f;
            const float OutAlpha = SrcAlpha + DstAlpha * (1.0f - SrcAlpha);
            if (OutAlpha <= 0.0f)
                continue;

            const auto BlendChannel = [&](size_t Channel) -> uint8_t
            {
                const float SrcValue = (float)Source.Rgba[SrcOffset + Channel] / 255.0f;
                const float DstValue = (float)Dest[DstOffset + Channel] / 255.0f;
                const float OutValue =
                    (SrcValue * SrcAlpha + DstValue * DstAlpha * (1.0f - SrcAlpha)) / OutAlpha;
                return (uint8_t)std::clamp((int)std::lround(OutValue * 255.0f), 0, 255);
            };

            Dest[DstOffset + 0] = BlendChannel(0);
            Dest[DstOffset + 1] = BlendChannel(1);
            Dest[DstOffset + 2] = BlendChannel(2);
            Dest[DstOffset + 3] = (uint8_t)std::clamp((int)std::lround(OutAlpha * 255.0f), 0, 255);
        }
    }
}

} // namespace Resource::Graphics
