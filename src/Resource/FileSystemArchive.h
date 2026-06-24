#pragma once

#include "Resource/IArchiveReader.h"

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
