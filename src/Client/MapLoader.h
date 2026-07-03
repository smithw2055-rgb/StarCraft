#pragma once

#include <optional>
#include <string>
#include <vector>

#include "Resource/IArchiveReader.h"

#include "Client/Entity.h"
#include "Game/Map/ChkParser.h"

namespace Game::Client
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
        std::vector<uint8_t> RawChkData;
        std::string ChkPath;
        bool Loaded = false;
        std::string ErrorMessage;
    };

    // Load the first scenario.chk found inside the archive at ScmPath
    static Result Load(const std::string& ScmPath);

    // Try to auto-detect a map under InstallPath/maps/BroodWar/*.scm
    static std::string FindDefaultMap(const std::string& InstallPath);

    // Access raw CHK data for trigger/replay parsing
    static std::vector<uint8_t> GetRawChkData() { return sLastChkData; }

private:
    static std::vector<uint8_t> sLastChkData;
};

} // namespace Game::Client
