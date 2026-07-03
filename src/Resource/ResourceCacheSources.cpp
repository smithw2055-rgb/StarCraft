#include "Resource/ResourceCache.h"

#include "Core/Utils/Logger.h"

#include "Resource/CascArchive.h"
#include "Resource/FileSystemArchive.h"
#include "Resource/MpqArchive.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

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
}

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
        if (Ext == ".vx4")
            AddUniquePath(Candidates, Seen, "tileset/" + Lower.substr(8) + "ex");
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

