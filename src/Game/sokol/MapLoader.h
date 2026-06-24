#pragma once

#include <optional>
#include <string>

#include "Resource/sokol/ResourceCacheSokol.h"
#include "Resource/IArchiveReader.h"

#include "Game/sokol/Entity.h"
#include "Game/Map/ChkParser.h"

namespace Game::SokolDemo
{

// Read a .scm/.scx file (raw MPQ-style archive) and produce a GameMap + spawn units.
class MapLoader
{
public:
    struct Result
    {
        GameMap Map;
        std::vector<StartLocation> ChkStartLocations;
        std::vector<Game::Map::PlacedUnit> ChkUnits;
        bool Loaded = false;
        std::string ErrorMessage;
    };

    // Load the first scenario.chk found inside the archive at ScmPath
    static Result Load(const std::string& ScmPath);

    // Try to auto-detect a map under InstallPath/maps/BroodWar/*.scm
    static std::string FindDefaultMap(const std::string& InstallPath);
};

} // namespace Game::SokolDemo
