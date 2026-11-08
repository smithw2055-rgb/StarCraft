#include "Resource/IArchiveReader.h"
#ifdef HAS_STORMLIB

#endif
#ifdef HAS_CASCLIB

#endif

#include "Core/Utils/Logger.h"
#include <filesystem>

namespace Resource
{

std::unique_ptr<IArchiveReader> CreateArchiveReader(ArchiveType Type)
{
    switch (Type)
    {
#ifdef HAS_STORMLIB
    case ArchiveType::Mpq:
        return std::make_unique<MpqArchive>();
#endif
#ifdef HAS_CASCLIB
    case ArchiveType::Casc:
        return std::make_unique<CascArchive>();
#endif
    default:
        return nullptr;
    }
}

std::unique_ptr<IArchiveReader> CreateBestArchiveForPath(const std::string& Path)
{
    Core::Utils::Logger::Info("Archive", "CreateBestArchiveForPath: " + Path);

    std::error_code Ec;
    const bool IsDirectory = std::filesystem::is_directory(std::filesystem::path(Path), Ec);
    if (IsDirectory)
    {
        auto FsReader = std::make_unique<FileSystemArchive>();
        if (FsReader->Open(Path))
        {
            Core::Utils::Logger::Info("Archive", "Opened as FileSystem: " + Path);
            return FsReader;
        }
        Core::Utils::Logger::Warning("Archive", "FileSystem open failed: " + Path);
    }
    else
    {
        Core::Utils::Logger::Info("Archive", "Skipping FileSystem reader for non-directory path");
    }

#ifdef HAS_STORMLIB
    Core::Utils::Logger::Info("Archive", "Trying MPQ reader: " + Path);
    auto MpqReader = std::make_unique<MpqArchive>();
    if (MpqReader->Open(Path))
    {
        Core::Utils::Logger::Info("Archive", "Opened as MPQ: " + Path);
        return MpqReader;
    }
    Core::Utils::Logger::Warning("Archive", "MPQ open failed: " + Path);
#else
    Core::Utils::Logger::Info("Archive", "MPQ reader unavailable at compile time");
#endif

#ifdef HAS_CASCLIB
    Core::Utils::Logger::Info("Archive", "Trying CASC reader: " + Path);
    auto CascReader = std::make_unique<CascArchive>();
    if (CascReader->Open(Path))
    {
        Core::Utils::Logger::Info("Archive", "Opened as CASC: " + Path);
        return CascReader;
    }
    Core::Utils::Logger::Warning("Archive", "CASC open failed: " + Path);
#else
    Core::Utils::Logger::Info("Archive", "CASC reader unavailable at compile time");
#endif

    Core::Utils::Logger::Warning("Archive", "No reader could open: " + Path);
    return nullptr;
}

} // namespace Resource

// === Consolidated from src/Resource/GameDetector.cpp ===
#include "Resource/IArchiveReader.h"
#include "Core/Utils/Logger.h"

#ifdef _WIN32
#include <windows.h>
#include <winreg.h>
#endif

#include <cstdio>
#include <sys/stat.h>

namespace Resource
{

static bool FileExists(const std::string& Path)
{
    struct stat Buf;
    return stat(Path.c_str(), &Buf) == 0;
}

bool GameDetector::CheckMpqExists(const std::string& Dir, const std::string& MpqName)
{
    std::string Path = Dir + "/" + MpqName;
    if (FileExists(Path))
        return true;
    Path = Dir + "\\" + MpqName;
    return FileExists(Path);
}

bool GameDetector::CheckCascExists(const std::string& Dir)
{
    // CASC storage has a data/ directory with .idx files
    // StarCraft: Remastered uses "Data/data/" (capital D), classic CASC uses "data/"
    std::vector<std::string> Candidates = {
        Dir + "/Data/data/data.000",
        Dir + "\\Data\\data\\data.000",
        Dir + "/data/data.000",
        Dir + "\\data\\data.000",
    };
    for (auto& C : Candidates)
    {
        if (FileExists(C))
            return true;
    }
    return false;
}

std::vector<std::string> GameDetector::GetCommonPaths()
{
    std::vector<std::string> Paths;

#ifdef _WIN32
    // Check registry
    HKEY Keys[] = {
        HKEY_LOCAL_MACHINE,
        HKEY_CURRENT_USER
    };

    const char* SubKeys[] = {
        "SOFTWARE\\Blizzard Entertainment\\StarCraft",
        "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\StarCraft",
        "SOFTWARE\\Wow6432Node\\Blizzard Entertainment\\StarCraft"
    };

    for (auto HKey : Keys)
    {
        for (const char* SubKey : SubKeys)
        {
            HKEY RegKey;
            if (RegOpenKeyExA(HKey, SubKey, 0, KEY_READ | KEY_WOW64_64KEY, &RegKey) == ERROR_SUCCESS)
            {
                char Value[512] = {};
                DWORD ValueSize = sizeof(Value);
                DWORD Type = 0;
                if (RegQueryValueExA(RegKey, "InstallPath", nullptr, &Type,
                                     (LPBYTE)Value, &ValueSize) == ERROR_SUCCESS)
                {
                    Paths.push_back(Value);
                }
                RegCloseKey(RegKey);
            }
        }
    }

    // Common paths
    Paths.push_back("C:\\Program Files\\StarCraft");
    Paths.push_back("C:\\Program Files (x86)\\StarCraft");
    Paths.push_back("C:\\Program Files\\StarCraft Remastered");
    Paths.push_back("C:\\Program Files (x86)\\StarCraft Remastered");
#endif

    // Also check current directory for explicit classic installs.
    Paths.push_back(".");
    Paths.push_back("./StarCraft");
    Paths.push_back("./StarCraft Remastered");

    return Paths;
}

GameInstallInfo GameDetector::CheckPath(const std::string& Path)
{
    GameInstallInfo Info;
    Info.InstallPath = Path;

    // Check for classic MPQ files
    if (CheckMpqExists(Path, "StarDat.mpq") ||
        CheckMpqExists(Path, "BrooDat.mpq") ||
        CheckMpqExists(Path, "patch_rt.mpq") ||
        CheckMpqExists(Path, "install.exe"))
    {
        Info.Type = ArchiveType::Mpq;
        Info.StarDatPath = Path + "/StarDat.mpq";
        Info.BrooDatPath = Path + "/BrooDat.mpq";
        Info.PatchRtPath = Path + "/patch_rt.mpq";

        Core::Utils::Logger::Info("GameDetector",
            "Found classic StarCraft at: " + Path);
        return Info;
    }

    // Check for Remastered CASC storage
    if (CheckCascExists(Path))
    {
        Info.Type = ArchiveType::Casc;

        // Determine the CASC root (directory containing data/, indices/, config/)
        if (FileExists(Path + "/Data/data/data.000") || FileExists(Path + "\\Data\\data\\data.000"))
            Info.CascDataPath = Path + "/Data";
        else
            Info.CascDataPath = Path + "/data";

        Core::Utils::Logger::Info("GameDetector",
            "Found StarCraft Remastered at: " + Info.CascDataPath);
        return Info;
    }

    return Info;  // Invalid
}

GameInstallInfo GameDetector::Detect()
{
    Core::Utils::Logger::Info("GameDetector",
        "Detecting StarCraft installation...");

    auto Paths = GetCommonPaths();
    for (const auto& Path : Paths)
    {
        auto Info = CheckPath(Path);
        if (Info.IsValid())
            return Info;
    }

    Core::Utils::Logger::Warning("GameDetector",
        "StarCraft installation not found automatically");
    return {};
}

} // namespace Resource

// === Consolidated from src/Resource/FileSystemArchive.cpp ===
#include "Resource/IArchiveReader.h"
#include "Core/Utils/Logger.h"

#include <fstream>
#include <filesystem>
#include <algorithm>

namespace Resource
{

FileSystemArchive::FileSystemArchive() = default;

FileSystemArchive::~FileSystemArchive()
{
    Close();
}

bool FileSystemArchive::Open(const std::string& ArchivePath)
{
    if (ArchivePath.empty())
    {
        Core::Utils::Logger::Warning("FileSystemArchive", "Empty archive path");
        return false;
    }

    // Check if path exists and is a directory
    std::filesystem::path Path(ArchivePath);
    if (!std::filesystem::exists(Path) || !std::filesystem::is_directory(Path))
    {
        Core::Utils::Logger::Warning("FileSystemArchive", 
            "Path does not exist or is not a directory: " + ArchivePath);
        return false;
    }

    mBasePath = ArchivePath;
    mIsOpen = true;
    
    Core::Utils::Logger::Info("FileSystemArchive", "Opened directory: " + mBasePath);
    return true;
}

void FileSystemArchive::Close()
{
    mBasePath.clear();
    mIsOpen = false;
}

std::string FileSystemArchive::GetFullPath(const std::string& FilePath) const
{
    if (mBasePath.empty())
        return FilePath;

    // Convert path separators to native format
    std::string NormalizedPath = FilePath;
    for (char& C : NormalizedPath)
    {
        if (C == '\\' || C == '/')
            C = std::filesystem::path::preferred_separator;
    }

    const char Separator = static_cast<char>(std::filesystem::path::preferred_separator);
    return mBasePath + Separator + NormalizedPath;
}

bool FileSystemArchive::FileExists(const std::string& Path) const
{
    return std::filesystem::exists(Path) && std::filesystem::is_regular_file(Path);
}

bool FileSystemArchive::HasFile(const std::string& FilePath) const
{
    if (!mIsOpen)
        return false;

    std::string FullPath = GetFullPath(FilePath);
    return FileExists(FullPath);
}

std::optional<std::vector<uint8_t>> FileSystemArchive::ReadFile(const std::string& FilePath)
{
    if (!mIsOpen)
    {
        Core::Utils::Logger::Warning("FileSystemArchive", "Archive not open");
        return std::nullopt;
    }

    std::string FullPath = GetFullPath(FilePath);
    
    if (!FileExists(FullPath))
    {
        // Try with different case variations
        std::string UpperPath = FullPath;
        std::transform(UpperPath.begin(), UpperPath.end(), UpperPath.begin(), ::toupper);
        
        std::string LowerPath = FullPath;
        std::transform(LowerPath.begin(), LowerPath.end(), LowerPath.begin(), ::tolower);
        
        if (FileExists(UpperPath))
            FullPath = UpperPath;
        else if (FileExists(LowerPath))
            FullPath = LowerPath;
        else
        {
            Core::Utils::Logger::Warning("FileSystemArchive", 
                "File not found: " + FilePath);
            return std::nullopt;
        }
    }

    // Read file
    std::ifstream File(FullPath, std::ios::binary | std::ios::ate);
    if (!File.is_open())
    {
        Core::Utils::Logger::Warning("FileSystemArchive", 
            "Failed to open file: " + FullPath);
        return std::nullopt;
    }

    std::streamsize Size = File.tellg();
    File.seekg(0, std::ios::beg);

    std::vector<uint8_t> Buffer(Size);
    if (!File.read(reinterpret_cast<char*>(Buffer.data()), Size))
    {
        Core::Utils::Logger::Warning("FileSystemArchive", 
            "Failed to read file: " + FullPath);
        return std::nullopt;
    }

    return Buffer;
}

std::vector<ArchiveFileInfo> FileSystemArchive::ListFiles()
{
    std::vector<ArchiveFileInfo> Files;
    
    if (!mIsOpen)
        return Files;

    try
    {
        for (const auto& Entry : std::filesystem::recursive_directory_iterator(mBasePath))
        {
            if (Entry.is_regular_file())
            {
                ArchiveFileInfo Info;
                Info.FilePath = Entry.path().string();
                Info.FileSize = Entry.file_size();
                
                // Remove base path prefix
                if (Info.FilePath.substr(0, mBasePath.size()) == mBasePath)
                    Info.FilePath = Info.FilePath.substr(mBasePath.size() + 1);
                
                Files.push_back(Info);
            }
        }
    }
    catch (const std::exception& e)
    {
        Core::Utils::Logger::Warning("FileSystemArchive", 
            "Error listing files: " + std::string(e.what()));
    }

    return Files;
}

} // namespace Resource
