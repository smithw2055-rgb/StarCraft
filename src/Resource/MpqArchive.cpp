#include "Resource/MpqArchive.h"
#include "Core/Utils/Logger.h"

#include <cstring>
#include <algorithm>

#ifdef HAS_STORMLIB
#include <StormLib.h>
#endif

namespace Resource
{

MpqArchive::MpqArchive()
{
}

MpqArchive::~MpqArchive()
{
    Close();
}

#ifdef HAS_STORMLIB

// Normalize MPQ internal paths: forward slash â†?backslash
static std::string NormalizeMpqPath(const std::string& Path)
{
    std::string Result = Path;
    for (auto& Ch : Result)
    {
        if (Ch == '/')
            Ch = '\\';
    }
    return Result;
}

static bool TryOpenArchivePath(const std::string& ArchivePath, HANDLE* OutHandle)
{
    if (SFileOpenArchive(ArchivePath.c_str(), 0, MPQ_OPEN_READ_ONLY, OutHandle))
        return true;

    std::string AltPath = NormalizeMpqPath(ArchivePath);
    if (AltPath != ArchivePath)
        return SFileOpenArchive(AltPath.c_str(), 0, MPQ_OPEN_READ_ONLY, OutHandle);

    return false;
}

bool MpqArchive::Open(const std::string& ArchivePath)
{
    if (mIsOpen)
        Close();

    mArchivePath = ArchivePath;
    std::string NormalizedArchivePath = NormalizeMpqPath(ArchivePath);

    if (!TryOpenArchivePath(ArchivePath, &mArchiveHandle))
    {
        DWORD Err = GetLastError();
        Core::Utils::Logger::Warning("MpqArchive", "Failed to open: " + ArchivePath);
        Core::Utils::Logger::Warning("MpqArchive",
            "  normalized path: " + NormalizedArchivePath +
            " error=" + std::to_string(Err));
        mArchiveHandle = nullptr;
        return false;
    }

    mIsOpen = true;
    Core::Utils::Logger::Info("MpqArchive",
        "Opened: " + ArchivePath + " (normalized: " + NormalizedArchivePath + ")");
    return true;
}

bool MpqArchive::OpenWithPatch(const std::string& BasePath, const std::string& PatchPath)
{
    if (mIsOpen)
        Close();

    // Open base MPQ (e.g., StarDat.mpq)
    if (!TryOpenArchivePath(BasePath, &mArchiveHandle))
    {
        DWORD Err = GetLastError();
        Core::Utils::Logger::Warning("MpqArchive", "Failed to open base: " + BasePath);
        Core::Utils::Logger::Warning("MpqArchive",
            "  normalized base: " + NormalizeMpqPath(BasePath) +
            " error=" + std::to_string(Err));
        mArchiveHandle = nullptr;
        return false;
    }

    // Attach patch MPQ (e.g., BrooDat.mpq files under "arr\")
    // The prefix "arr" means StormLib maps root requests to \arr\path in the patch.
    if (!SFileOpenPatchArchive(mArchiveHandle, PatchPath.c_str(), "arr", 0))
    {
        Core::Utils::Logger::Warning("MpqArchive", "Failed to attach patch: " + PatchPath
            + " (base will still work)");
    }
    else
    {
        Core::Utils::Logger::Info("MpqArchive", "Attached patch: " + PatchPath);
    }

    mIsOpen = true;
    Core::Utils::Logger::Info("MpqArchive", "Opened base+patch: " + BasePath + " + " + PatchPath);
    return true;
}

void MpqArchive::Close()
{
    if (mArchiveHandle)
    {
        SFileCloseArchive(mArchiveHandle);
        mArchiveHandle = nullptr;
    }
    mIsOpen = false;
}

bool MpqArchive::HasFile(const std::string& FilePath) const
{
    if (!mIsOpen || !mArchiveHandle)
        return false;

    return SFileHasFile(mArchiveHandle, NormalizeMpqPath(FilePath).c_str());
}

std::optional<std::vector<uint8_t>> MpqArchive::ReadFile(const std::string& FilePath)
{
    if (!mIsOpen || !mArchiveHandle)
        return std::nullopt;

    std::string NormalizedPath = NormalizeMpqPath(FilePath);
    Core::Utils::Logger::Debug("MpqArchive", "Opening file: " + NormalizedPath);

    HANDLE FileHandle;
    if (!SFileOpenFileEx(mArchiveHandle, NormalizedPath.c_str(), SFILE_OPEN_FROM_MPQ, &FileHandle))
    {
        DWORD Err = GetLastError();
        Core::Utils::Logger::Warning("MpqArchive",
            "Failed to open " + NormalizedPath + " (error=" + std::to_string(Err) + ")");
        return std::nullopt;
    }

    DWORD FileSizeHigh = 0;
    DWORD FileSize = SFileGetFileSize(FileHandle, &FileSizeHigh);

    std::vector<uint8_t> Buffer;
    Buffer.resize(FileSize);

    DWORD BytesRead = 0;
    bool Success = SFileReadFile(FileHandle, Buffer.data(), FileSize, &BytesRead, nullptr);

    SFileCloseFile(FileHandle);

    if (!Success || BytesRead != FileSize)
    {
        Core::Utils::Logger::Warning("MpqArchive", "ReadFile failed or size mismatch: " + FilePath);
        return std::nullopt;
    }

    return Buffer;
}

std::vector<ArchiveFileInfo> MpqArchive::ListFiles()
{
    std::vector<ArchiveFileInfo> Files;

    SFILE_FIND_DATA FindData = {};
    HANDLE FindHandle = SFileFindFirstFile(mArchiveHandle, "*", &FindData, nullptr);
    if (FindHandle)
    {
        do
        {
            ArchiveFileInfo Info;
            Info.FilePath = FindData.cFileName;
            Info.FileSize = FindData.dwFileSize;
            Files.push_back(Info);
        }
        while (SFileFindNextFile(FindHandle, &FindData));

        SFileFindClose(FindHandle);
    }

    return Files;
}

bool MpqArchive::AddListFile(const std::string& FilePath)
{
    if (!mIsOpen || !mArchiveHandle)
        return false;
    return SFileAddListFile(mArchiveHandle, FilePath.c_str());
}

#else // !HAS_STORMLIB

bool MpqArchive::Open(const std::string& ArchivePath)
{
    (void)ArchivePath;
    Core::Utils::Logger::Warning("MpqArchive", "StormLib not available");
    return false;
}

void MpqArchive::Close()
{
    mIsOpen = false;
}

bool MpqArchive::HasFile(const std::string& FilePath) const
{
    (void)FilePath;
    return false;
}

std::optional<std::vector<uint8_t>> MpqArchive::ReadFile(const std::string& FilePath)
{
    (void)FilePath;
    return std::nullopt;
}

std::vector<ArchiveFileInfo> MpqArchive::ListFiles()
{
    return {};
}

bool MpqArchive::AddListFile(const std::string& FilePath)
{
    (void)FilePath;
    return false;
}

#endif // HAS_STORMLIB

} // namespace Resource
