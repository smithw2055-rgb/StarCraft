#include "Resource/FileSystemArchive.h"
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
