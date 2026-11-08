#pragma once

#include <string>
#include <vector>
#include <memory>
#include <optional>

namespace Resource
{

struct ArchiveFileInfo
{
    std::string FilePath;
    size_t FileSize = 0;
};

class IArchiveReader
{
public:
    virtual ~IArchiveReader() = default;

    virtual bool Open(const std::string& ArchivePath) = 0;
    virtual void Close() = 0;

    virtual bool HasFile(const std::string& FilePath) const = 0;
    virtual std::optional<std::vector<uint8_t>> ReadFile(const std::string& FilePath) = 0;
    virtual std::vector<ArchiveFileInfo> ListFiles() = 0;

    virtual bool IsOpen() const = 0;
    virtual std::string GetArchiveTypeName() const = 0;
};

enum class ArchiveType
{
    Unknown,
    Mpq,      // 经典 StarCraft / Brood War
    Casc      // StarCraft: Remastered
};

struct GameInstallInfo
{
    std::string InstallPath;
    ArchiveType Type = ArchiveType::Unknown;
    std::string StarDatPath;    // MPQ: StarDat.mpq
    std::string BrooDatPath;    // MPQ: BrooDat.mpq
    std::string PatchRtPath;    // MPQ: patch_rt.mpq
    std::string CascDataPath;   // CASC: data/ directory
    bool IsValid() const { return Type != ArchiveType::Unknown; }
};

std::unique_ptr<IArchiveReader> CreateArchiveReader(ArchiveType Type);
std::unique_ptr<IArchiveReader> CreateBestArchiveForPath(const std::string& Path);

} // namespace Resource

// === Consolidated from src/Resource/GameDetector.h ===
#include <string>
#include <vector>
#include <optional>



namespace Resource
{

class GameDetector
{
public:
    // Auto-detect StarCraft installation
    static GameInstallInfo Detect();

    // Check a specific path for StarCraft installation
    static GameInstallInfo CheckPath(const std::string& Path);

    // Get list of common StarCraft install paths
    static std::vector<std::string> GetCommonPaths();

private:
    static bool CheckMpqExists(const std::string& Dir, const std::string& MpqName);
    static bool CheckCascExists(const std::string& Dir);
};

} // namespace Resource
