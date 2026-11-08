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

// === Consolidated Archive Headers ===


#include <string>
#include <vector>
#include <optional>

namespace Resource
{

class FileSystemArchive : public IArchiveReader
{
public:
    FileSystemArchive();
    ~FileSystemArchive() override;

    bool Open(const std::string& ArchivePath) override;
    void Close() override;

    bool HasFile(const std::string& FilePath) const override;
    std::optional<std::vector<uint8_t>> ReadFile(const std::string& FilePath) override;
    std::vector<ArchiveFileInfo> ListFiles() override;

    bool IsOpen() const override { return mIsOpen; }
    std::string GetArchiveTypeName() const override { return "FileSystem"; }

private:
    std::string mBasePath;
    bool mIsOpen = false;
    
    std::string GetFullPath(const std::string& FilePath) const;
    bool FileExists(const std::string& Path) const;
};

} // namespace Resource



#include <string>
#include <vector>
#include <optional>

// Forward declare CascLib handle
typedef void* HANDLE;
struct _CASC_STORAGE;
struct _CASC_FILE;

namespace Resource
{

class CascArchive : public IArchiveReader
{
public:
    CascArchive();
    ~CascArchive() override;

    bool Open(const std::string& StoragePath) override;
    void Close() override;

    bool HasFile(const std::string& FilePath) const override;
    std::optional<std::vector<uint8_t>> ReadFile(const std::string& FilePath) override;
    std::vector<ArchiveFileInfo> ListFiles() override;

    bool IsOpen() const override { return mIsOpen; }
    std::string GetArchiveTypeName() const override { return "CASC"; }

private:
    HANDLE mStorageHandle = nullptr;
    bool mIsOpen = false;
    std::string mStoragePath;
};

} // namespace Resource



namespace Resource
{

class MpqArchive : public IArchiveReader
{
public:
    MpqArchive();
    ~MpqArchive() override;

    bool Open(const std::string& ArchivePath) override;
    bool OpenWithPatch(const std::string& BasePath, const std::string& PatchPath);
    void Close() override;

    bool HasFile(const std::string& FilePath) const override;
    std::optional<std::vector<uint8_t>> ReadFile(const std::string& FilePath) override;
    std::vector<ArchiveFileInfo> ListFiles() override;

    // Attach an external listfile for filename resolution
    bool AddListFile(const std::string& FilePath);

    bool IsOpen() const override { return mIsOpen; }
    std::string GetArchiveTypeName() const override { return "MPQ"; }

private:
    HANDLE mArchiveHandle = nullptr;
    bool mIsOpen = false;
    std::string mArchivePath;
};

} // namespace Resource
