#pragma once

#include "Resource/IArchiveReader.h"

#include <string>
#include <vector>
#include <optional>

// Forward declare StormLib handle
typedef void* HANDLE;

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
