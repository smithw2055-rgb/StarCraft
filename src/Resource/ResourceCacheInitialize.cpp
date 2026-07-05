#include "Resource/ResourceCache.h"

#include "Core/Utils/Logger.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

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
    Core::Utils::Logger::Info("ResourceCache", "Initializing with: " + GameInstallPath);

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
