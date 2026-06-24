#pragma once

#include "Resource/IArchiveReader.h"

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
