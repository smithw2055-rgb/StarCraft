#pragma once

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Resource/IArchiveReader.h"

namespace Engine::VFS
{

// ============================================================================
// VirtualFileSystemServer (Phase 56):
// Referenced from Godot 4 Virtual File System (res://, user://, DirAccess,
// FileAccess, ProjectSettings::load_resource_pack) & Unity Addressables/StreamingAssets:
// - Decouples engine servers and game modules from raw OS paths.
// - Supports mounting multiple physical directories or IArchiveReader packs
//   under logical URI schemes (e.g. "res://", "user://", "mods://") with
//   priority-based overlay resolution (higher priority overrides lower priority).
// ============================================================================
struct MountPoint
{
    std::string Scheme = "res://";
    std::string PhysicalRoot;
    Resource::IArchiveReader* Archive = nullptr;
    int Priority = 0;
    bool ReadOnly = true;
};

class VirtualFileSystemServer
{
public:
    VirtualFileSystemServer() = default;

    void MountDirectory(std::string Scheme,
                        std::string PhysicalRoot,
                        int Priority = 0,
                        bool ReadOnly = true)
    {
        Scheme = NormalizeScheme(std::move(Scheme));
        std::replace(PhysicalRoot.begin(), PhysicalRoot.end(), '\\', '/');
        for (auto& Existing : mMounts)
        {
            if (Existing.Scheme == Scheme && Existing.PhysicalRoot == PhysicalRoot && Existing.Archive == nullptr)
            {
                Existing.Priority = Priority;
                Existing.ReadOnly = ReadOnly;
                SortMounts();
                return;
            }
        }
        mMounts.push_back(MountPoint{
            std::move(Scheme),
            std::move(PhysicalRoot),
            nullptr,
            Priority,
            ReadOnly});
        SortMounts();
    }

    void MountArchive(std::string Scheme,
                      Resource::IArchiveReader* Archive,
                      int Priority = 0)
    {
        if (!Archive)
            return;
        Scheme = NormalizeScheme(std::move(Scheme));
        for (auto& Existing : mMounts)
        {
            if (Existing.Scheme == Scheme && Existing.Archive == Archive)
            {
                Existing.Priority = Priority;
                SortMounts();
                return;
            }
        }
        mMounts.push_back(MountPoint{
            std::move(Scheme),
            "",
            Archive,
            Priority,
            true});
        SortMounts();
    }

    void UnmountScheme(std::string_view Scheme)
    {
        const std::string Norm = NormalizeScheme(std::string(Scheme));
        mMounts.erase(
            std::remove_if(mMounts.begin(), mMounts.end(),
                           [&](const MountPoint& M) { return M.Scheme == Norm; }),
            mMounts.end());
    }

    void Clear() noexcept
    {
        mMounts.clear();
        mInMemoryFiles.clear();
    }

    void RegisterMemoryFile(std::string Uri, std::vector<uint8_t> Bytes)
    {
        mInMemoryFiles[CanonicalizeUri(std::move(Uri))] = std::move(Bytes);
    }

    void RegisterMemoryText(std::string Uri, std::string_view Text)
    {
        mInMemoryFiles[CanonicalizeUri(std::move(Uri))] =
            std::vector<uint8_t>(Text.begin(), Text.end());
    }

    bool Exists(std::string_view Uri) const
    {
        const std::string NormUri = NormalizeUri(std::string(Uri));
        std::string Scheme;
        std::string RelPath;
        SplitUri(NormUri, Scheme, RelPath);
        const std::string CanonicalKey = Scheme + RelPath;

        if (mInMemoryFiles.find(CanonicalKey) != mInMemoryFiles.end())
            return true;

        namespace fs = std::filesystem;
        for (const auto& M : mMounts)
        {
            if (M.Scheme != Scheme)
                continue;
            if (M.Archive && M.Archive->IsOpen() && M.Archive->HasFile(RelPath))
                return true;
            if (!M.PhysicalRoot.empty())
            {
                std::error_code EC;
                if (fs::exists(fs::path(M.PhysicalRoot) / RelPath, EC))
                    return true;
            }
        }

        std::error_code EC;
        return fs::exists(fs::path(NormUri), EC);
    }

    std::string ResolvePhysicalPath(std::string_view Uri) const
    {
        const std::string NormUri = NormalizeUri(std::string(Uri));
        std::string Scheme;
        std::string RelPath;
        SplitUri(NormUri, Scheme, RelPath);

        namespace fs = std::filesystem;
        std::string FallbackCandidate;
        for (const auto& M : mMounts)
        {
            if (M.Scheme != Scheme || M.PhysicalRoot.empty())
                continue;
            const fs::path FullPath = fs::path(M.PhysicalRoot) / RelPath;
            if (FallbackCandidate.empty())
                FallbackCandidate = FullPath.generic_string();
            std::error_code EC;
            if (fs::exists(FullPath, EC))
                return FullPath.generic_string();
        }
        return FallbackCandidate.empty() ? RelPath : FallbackCandidate;
    }

    std::optional<std::vector<uint8_t>> ReadBytes(std::string_view Uri) const
    {
        const std::string NormUri = NormalizeUri(std::string(Uri));
        std::string Scheme;
        std::string RelPath;
        SplitUri(NormUri, Scheme, RelPath);
        const std::string CanonicalKey = Scheme + RelPath;

        auto MemIt = mInMemoryFiles.find(CanonicalKey);
        if (MemIt != mInMemoryFiles.end())
            return MemIt->second;

        namespace fs = std::filesystem;
        for (const auto& M : mMounts)
        {
            if (M.Scheme != Scheme)
                continue;
            if (M.Archive && M.Archive->IsOpen())
            {
                if (auto Bytes = M.Archive->ReadFile(RelPath))
                    return Bytes;
            }
            if (!M.PhysicalRoot.empty())
            {
                const fs::path FullPath = fs::path(M.PhysicalRoot) / RelPath;
                std::error_code EC;
                if (fs::exists(FullPath, EC) && fs::is_regular_file(FullPath, EC))
                {
                    std::ifstream In(FullPath, std::ios::binary);
                    if (In.is_open())
                    {
                        return std::vector<uint8_t>(
                            std::istreambuf_iterator<char>(In),
                            std::istreambuf_iterator<char>());
                    }
                }
            }
        }

        // Direct path fallback
        std::error_code EC;
        if (fs::exists(NormUri, EC) && fs::is_regular_file(NormUri, EC))
        {
            std::ifstream In(NormUri, std::ios::binary);
            if (In.is_open())
            {
                return std::vector<uint8_t>(
                    std::istreambuf_iterator<char>(In),
                    std::istreambuf_iterator<char>());
            }
        }
        return std::nullopt;
    }

    std::optional<std::string> ReadText(std::string_view Uri) const
    {
        auto Bytes = ReadBytes(Uri);
        if (!Bytes)
            return std::nullopt;
        return std::string(reinterpret_cast<const char*>(Bytes->data()), Bytes->size());
    }

    bool WriteText(std::string_view Uri, std::string_view Content)
    {
        const std::string NormUri = NormalizeUri(std::string(Uri));
        std::string Scheme;
        std::string RelPath;
        SplitUri(NormUri, Scheme, RelPath);

        namespace fs = std::filesystem;
        for (const auto& M : mMounts)
        {
            if (M.Scheme != Scheme || M.ReadOnly || M.PhysicalRoot.empty())
                continue;
            const fs::path FullPath = fs::path(M.PhysicalRoot) / RelPath;
            std::error_code EC;
            fs::create_directories(FullPath.parent_path(), EC);
            std::ofstream Out(FullPath, std::ios::binary | std::ios::trunc);
            if (!Out.is_open())
                return false;
            Out.write(Content.data(), static_cast<std::streamsize>(Content.size()));
            return Out.good();
        }

        // Fallback to in-memory writable store when no physical writable mount is configured
        RegisterMemoryText(Scheme + RelPath, Content);
        return true;
    }

    std::vector<std::string> ListFiles(std::string_view SchemeOrUriPrefix = "res://",
                                       std::string_view ExtensionFilter = "") const
    {
        std::string Scheme;
        std::string RelPrefix;
        SplitUri(NormalizeUri(std::string(SchemeOrUriPrefix)), Scheme, RelPrefix);

        std::vector<std::string> Results;
        namespace fs = std::filesystem;
        for (const auto& M : mMounts)
        {
            if (M.Scheme != Scheme || M.PhysicalRoot.empty())
                continue;
            const fs::path BaseDir = fs::path(M.PhysicalRoot) / RelPrefix;
            std::error_code EC;
            if (!fs::exists(BaseDir, EC) || !fs::is_directory(BaseDir, EC))
                continue;
            for (const auto& Entry : fs::recursive_directory_iterator(BaseDir, EC))
            {
                if (!Entry.is_regular_file(EC))
                    continue;
                if (!ExtensionFilter.empty() && Entry.path().extension().string() != ExtensionFilter)
                    continue;
                const auto Rel = fs::relative(Entry.path(), M.PhysicalRoot, EC).generic_string();
                const std::string FullUri = Scheme + Rel;
                if (std::find(Results.begin(), Results.end(), FullUri) == Results.end())
                    Results.push_back(FullUri);
            }
        }

        const std::string PrefixUri = Scheme + RelPrefix;
        for (const auto& [MemUri, _] : mInMemoryFiles)
        {
            if (MemUri.compare(0, PrefixUri.size(), PrefixUri) != 0)
                continue;
            if (!ExtensionFilter.empty() &&
                (MemUri.size() < ExtensionFilter.size() ||
                 MemUri.compare(MemUri.size() - ExtensionFilter.size(), ExtensionFilter.size(), ExtensionFilter) != 0))
            {
                continue;
            }
            if (std::find(Results.begin(), Results.end(), MemUri) == Results.end())
                Results.push_back(MemUri);
        }

        std::sort(Results.begin(), Results.end());
        return Results;
    }

    size_t GetMountCount() const noexcept { return mMounts.size(); }
    const std::vector<MountPoint>& GetMounts() const noexcept { return mMounts; }

private:
    static std::string NormalizeScheme(std::string Scheme)
    {
        if (Scheme.find("://") == std::string::npos)
            Scheme += "://";
        return Scheme;
    }

    static std::string NormalizeUri(std::string Uri)
    {
        std::replace(Uri.begin(), Uri.end(), '\\', '/');
        return Uri;
    }

    static std::string CanonicalizeUri(std::string Uri)
    {
        std::string Scheme;
        std::string RelPath;
        SplitUri(NormalizeUri(std::move(Uri)), Scheme, RelPath);
        return Scheme + RelPath;
    }

    static void SplitUri(const std::string& Uri, std::string& OutScheme, std::string& OutRelPath)
    {
        const auto Pos = Uri.find("://");
        if (Pos != std::string::npos)
        {
            OutScheme = Uri.substr(0, Pos + 3);
            OutRelPath = Uri.substr(Pos + 3);
        }
        else
        {
            OutScheme = "res://";
            OutRelPath = Uri;
        }
        while (!OutRelPath.empty() && OutRelPath.front() == '/')
            OutRelPath.erase(OutRelPath.begin());
    }

    void SortMounts()
    {
        std::stable_sort(mMounts.begin(), mMounts.end(),
                         [](const MountPoint& A, const MountPoint& B) {
                             return A.Priority > B.Priority;
                         });
    }

    std::vector<MountPoint> mMounts;
    std::unordered_map<std::string, std::vector<uint8_t>> mInMemoryFiles;
};

} // namespace Engine::VFS
