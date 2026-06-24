#include "Resource/IArchiveReader.h"
#ifdef HAS_STORMLIB
#include "Resource/MpqArchive.h"
#endif
#ifdef HAS_CASCLIB
#include "Resource/CascArchive.h"
#endif
#include "Resource/FileSystemArchive.h"
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
