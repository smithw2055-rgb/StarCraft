#pragma once

#include <string>
#include <vector>
#include <optional>

#include "Resource/IArchiveReader.h"

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
