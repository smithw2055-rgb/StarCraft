#include "Resource/CascArchive.h"
#include "Core/Utils/Logger.h"
#include <cstdio>

#ifdef HAS_CASCLIB
#include <CascLib.h>
#endif

namespace Resource
{

CascArchive::CascArchive()
{
}

CascArchive::~CascArchive()
{
    Close();
}

#ifdef HAS_CASCLIB

namespace
{
    std::optional<std::vector<uint8_t>> ReadCascHandle(HANDLE FileHandle)
    {
        if (!FileHandle) return std::nullopt;

        DWORD FileSizeHigh = 0;
        DWORD FileSize = CascGetFileSize(FileHandle, &FileSizeHigh);

        if (FileSize != CASC_INVALID_SIZE && FileSize > 0)
        {
            std::vector<uint8_t> Buffer(FileSize);
            DWORD BytesRead = 0;
            if (CascReadFile(FileHandle, Buffer.data(), FileSize, &BytesRead) && BytesRead > 0)
            {
                Buffer.resize(BytesRead);
                return Buffer;
            }
        }

        std::vector<uint8_t> Chunked;
        uint8_t Temp[65536];
        while (true)
        {
            DWORD ReadNow = 0;
            if (!CascReadFile(FileHandle, Temp, sizeof(Temp), &ReadNow))
                break;
            if (ReadNow == 0)
                break;
            Chunked.insert(Chunked.end(), Temp, Temp + ReadNow);
            if (ReadNow < sizeof(Temp))
                break;
        }
        if (Chunked.empty())
            return std::nullopt;
        return Chunked;
    }
}

bool CascArchive::Open(const std::string& StoragePath)
{
    if (mIsOpen)
        Close();

    mStoragePath = StoragePath;

    if (!CascOpenStorage(StoragePath.c_str(), 0, &mStorageHandle))
    {
        Core::Utils::Logger::Warning("CascArchive", "Failed to open: " + StoragePath);
        mStorageHandle = nullptr;
        return false;
    }

    mIsOpen = true;
    Core::Utils::Logger::Info("CascArchive", "Opened: " + StoragePath);
    return true;
}

void CascArchive::Close()
{
    if (mStorageHandle)
    {
        CascCloseStorage(mStorageHandle);
        mStorageHandle = nullptr;
    }
    mIsOpen = false;
}

bool CascArchive::HasFile(const std::string& FilePath) const
{
    if (!mIsOpen || !mStorageHandle)
        return false;

    CASC_FIND_DATA FindData;
    HANDLE FindHandle = CascFindFirstFile(mStorageHandle, FilePath.c_str(), &FindData, NULL);
    if (FindHandle)
        CascFindClose(FindHandle);
    return (FindHandle != NULL);
}

std::optional<std::vector<uint8_t>> CascArchive::ReadFile(const std::string& FilePath)
{
    if (!mIsOpen || !mStorageHandle)
        return std::nullopt;

    // Step 1: Try direct open by name (multiple locale masks)
    HANDLE FileHandle;
    for (DWORD Locale : { 0x00000000, 0x00000409 })
    {
        if (CascOpenFile(mStorageHandle, FilePath.c_str(), Locale, CASC_OPEN_BY_NAME, &FileHandle))
        {
            auto Result = ReadCascHandle(FileHandle);
            CascCloseFile(FileHandle);
            if (Result) return Result;
        }
    }

    // Step 2: Try with alternate separators (forward slash -> backslash)
    std::string Alt;
    for (char C : FilePath)
    {
        if (C == '/') Alt += '\\';
        else if (C == '\\') Alt += '/';
        else Alt += C;
    }
    if (Alt != FilePath)
    {
        for (DWORD Locale : { 0x00000000, 0x00000409 })
        {
            if (CascOpenFile(mStorageHandle, Alt.c_str(), Locale, CASC_OPEN_BY_NAME, &FileHandle))
            {
                auto Result = ReadCascHandle(FileHandle);
                CascCloseFile(FileHandle);
                if (Result) return Result;
            }
        }
    }

    // Step 3: Find the file via CascFindFirstFile.
    // SC:R CASC can store logical paths with mixed case.
    // Must save ALL find data BEFORE CascFindClose (Windows safety).
    CASC_FIND_DATA FindData;
    HANDLE FindHandle = CascFindFirstFile(mStorageHandle, FilePath.c_str(), &FindData, nullptr);
    if (!FindHandle && !Alt.empty() && Alt != FilePath)
        FindHandle = CascFindFirstFile(mStorageHandle, Alt.c_str(), &FindData, nullptr);
    if (!FindHandle)
        return std::nullopt;

    std::string FoundName = FindData.szFileName;
    char CKeyStr[33] = {}, EKeyStr[33] = {};
    for (int I = 0; I < 16; I++)
        snprintf(CKeyStr + I * 2, 3, "%02x", (unsigned char)FindData.CKey[I]);
    for (int I = 0; I < 16; I++)
        snprintf(EKeyStr + I * 2, 3, "%02x", (unsigned char)FindData.EKey[I]);
    CascFindClose(FindHandle);

    // 3b: Try opening by the found name (different case/suffix)
    if (FoundName != "SIZE" && !FoundName.empty() && FoundName != FilePath && FoundName != Alt)
    {
        for (DWORD Locale : { 0x00000000, 0x00000409 })
        {
            if (CascOpenFile(mStorageHandle, FoundName.c_str(), Locale, CASC_OPEN_BY_NAME, &FileHandle))
            {
                auto Result = ReadCascHandle(FileHandle);
                CascCloseFile(FileHandle);
                if (Result) return Result;
            }
        }
    }

    // 3c: For nameless entries, try CKey / EKey
    if (FoundName == "SIZE")
    {
        if (CascOpenFile(mStorageHandle, CKeyStr, 0, CASC_OPEN_BY_CKEY, &FileHandle))
        {
            auto Result = ReadCascHandle(FileHandle);
            CascCloseFile(FileHandle);
            if (Result) return Result;
        }
        if (CascOpenFile(mStorageHandle, EKeyStr, 0, CASC_OPEN_BY_EKEY, &FileHandle))
        {
            auto Result = ReadCascHandle(FileHandle);
            CascCloseFile(FileHandle);
            if (Result) return Result;
        }
    }

    return std::nullopt;
}

std::vector<ArchiveFileInfo> CascArchive::ListFiles()
{
    std::vector<ArchiveFileInfo> Files;
    CASC_FIND_DATA FindData;

    HANDLE FindHandle = CascFindFirstFile(mStorageHandle, "*", &FindData, NULL);
    if (FindHandle != NULL)
    {
        do
        {
            ArchiveFileInfo Info;
            Info.FilePath = FindData.szFileName;
            Info.FileSize = (uint32_t)FindData.FileSize;
            Files.push_back(Info);
        } while (CascFindNextFile(FindHandle, &FindData));

        CascFindClose(FindHandle);
    }

    return Files;
}

#else // !HAS_CASCLIB

bool CascArchive::Open(const std::string& StoragePath)
{
    (void)StoragePath;
    Core::Utils::Logger::Warning("CascArchive", "CascLib not available");
    return false;
}

void CascArchive::Close()
{
    mIsOpen = false;
}

bool CascArchive::HasFile(const std::string& FilePath) const
{
    (void)FilePath;
    return false;
}

std::optional<std::vector<uint8_t>> CascArchive::ReadFile(const std::string& FilePath)
{
    (void)FilePath;
    return std::nullopt;
}

std::vector<ArchiveFileInfo> CascArchive::ListFiles()
{
    return {};
}

#endif // HAS_CASCLIB

} // namespace Resource
