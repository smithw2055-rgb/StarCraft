#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Resource::Graphics
{

class ResourceCache;

class ArchiveAssetSourceView
{
public:
    explicit ArchiveAssetSourceView(ResourceCache* Cache = nullptr)
        : mCache(Cache)
    {
    }

    bool HasCache() const { return mCache != nullptr; }

    std::optional<std::vector<uint8_t>> ReadFile(const std::string& Path) const;
    std::optional<std::vector<uint8_t>> ReadWithFallback(const std::string& Path) const;
    std::vector<std::string> DebugBuildExtractedAssetCandidates(const std::string& Path) const;

private:
    ResourceCache* mCache = nullptr;
};

} // namespace Resource::Graphics