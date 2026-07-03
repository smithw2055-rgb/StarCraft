#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "Resource/IArchiveReader.h"

namespace Resource::Graphics
{

struct ArchiveSourceState
{
    std::unique_ptr<IArchiveReader> Archive;
    std::unique_ptr<IArchiveReader> Archive2;
    std::unique_ptr<IArchiveReader> Archive3;  // patch_rt (highest priority)
    std::string ListfilePath;

    bool IsCasc = false;
    bool UseExtractedAssets = false;
    std::string AssetsRoot;

    std::vector<std::string> CascPrefixes = {"", "base1/", "sc1/", "sc1bw/"};
    std::unordered_map<std::string, std::string> CascPathCache;
};

} // namespace Resource::Graphics
