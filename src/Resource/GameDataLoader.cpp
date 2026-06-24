#include "Resource/GameDataLoader.h"
#include "Resource/TblDecoder.h"
#include "Core/Utils/Logger.h"

namespace Resource
{

// Brood War stores .dat files in arr/; classic stores them at root.
static std::optional<std::vector<uint8_t>> ReadDataFile(IArchiveReader& Archive, const std::string& Name)
{
    auto Data = Archive.ReadFile(Name);
    if (Data)
        return Data;
    return Archive.ReadFile("arr/" + Name);
}

GameDataLoader::GameDataLoader()
{
}

GameDataLoader::~GameDataLoader() = default;

bool GameDataLoader::LoadFromInstall(const std::string& InstallPath)
{
    auto Archive = CreateBestArchiveForPath(InstallPath);
    if (!Archive)
    {
        auto Mpq = CreateArchiveReader(ArchiveType::Mpq);
        std::string StarDat = InstallPath + "/StarDat.mpq";
        if (Mpq && Mpq->Open(StarDat))
        {
            Archive = std::move(Mpq);
        }
        else
        {
            Core::Utils::Logger::Error("GameDataLoader",
                "Cannot find StarCraft data at: " + InstallPath);
            return false;
        }
    }

    bool AllOk = true;
    AllOk = LoadUnitData(*Archive) && AllOk;
    AllOk = LoadWeaponData(*Archive) && AllOk;
    AllOk = LoadFlingyData(*Archive) && AllOk;
    AllOk = LoadSpriteData(*Archive) && AllOk;
    AllOk = LoadImageData(*Archive) && AllOk;
    AllOk = LoadUpgradeData(*Archive) && AllOk;
    AllOk = LoadTechData(*Archive) && AllOk;
    LoadImageTblData(*Archive);
    LoadStatTxtTbl(*Archive);
    LoadUnitNamesTbl(*Archive);
    mUnitPalette = LoadUnitPalette(*Archive);

    Archive->Close();

    mIsLoaded = AllOk;
    Core::Utils::Logger::Info("GameDataLoader",
        std::string("Game data loaded: ") + (AllOk ? "OK" : "Partial"));
    return AllOk;
}

bool GameDataLoader::LoadUnitData(IArchiveReader& Archive)
{
    auto Data = ReadDataFile(Archive, "units.dat");
    if (!Data) return false;
    mUnits = DatDecoder::DecodeUnits(*Data);

    // Assign names from known unit list
    const char* UnitNames[] = {
        "Terran Marine", "Terran Ghost", "Terran Vulture", "Terran Goliath",
        "Terran Siege Tank", "SCV", "Terran Wraith", "Terran Science Vessel",
        "Terran Dropship", "Terran Battlecruiser", "Terran Valkyrie",
        "Terran Medic", "Firebat", "Terran H over Goliath",
        // Zerg
        "Zerg Larva", "Zerg Egg", "Zerg Zergling", "Zerg Hydralisk",
        "Zerg Ultralisk", "Zerg Broodling", "Zerg Drone", "Zerg Overlord",
        "Zerg Mutalisk", "Zerg Guardian", "Zerg Queen", "Zerg Defiler",
        "Zerg Scourge", "Zerg Infested Terran", "Zerg Lurker",
        "Zerg Devourer",
        // Protoss
        "Protoss Probe", "Protoss Zealot", "Protoss Dragoon",
        "Protoss High Templar", "Protoss Dark Templar", "Protoss Archon",
        "Protoss Dark Archon", "Protoss Reaver", "Protoss Shuttle",
        "Protoss Scout", "Protoss Corsair", "Protoss Carrier",
        "Protoss Arbiter", "Protoss Observer",
        // Buildings - Terran
        "Terran Command Center", "Terran Supply Depot", "Terran Refinery",
        "Terran Barracks", "Terran Academy", "Terran Factory",
        "Terran Starport", "Terran Engineering Bay", "Terran Armory",
        "Terran Bunker", "Terran Missile Turret", "Terran Science Facility",
        "Terran Nuclear Silo", "Terran Comsat Station", "Terran Machine Shop",
        // Buildings - Zerg
        "Zerg Hatchery", "Zerg Lair", "Zerg Hive", "Zerg Spawning Pool",
        "Zerg Evolution Chamber", "Zerg Hydralisk Den", "Zerg Spire",
        "Zerg Greater Spire", "Zerg Queen's Nest", "Zerg Defiler Mound",
        "Zerg Ultralisk Cavern", "Zerg Nydus Canal", "Zerg Extractor",
        "Zerg Sunken Colony", "Zerg Spore Colony", "Zerg Creep Colony",
        // Buildings - Protoss
        "Protoss Nexus", "Protoss Pylon", "Protoss Gateway",
        "Protoss Forge", "Protoss Cybernetics Core", "Protoss Photon Cannon",
        "Protoss Shield Battery", "Protoss Robotics Facility",
        "Protoss Robotics Support Bay", "Protoss Observatory",
        "Protoss Stargate", "Protoss Fleet Beacon", "Protoss Citadel of Adun",
        "Protoss Templar Archives", "Protoss Arbiter Tribunal",
        "Protoss Assimilator"
    };

    for (size_t I = 0; I < mUnits.size(); I++)
    {
        if (I < mUnitNamesTbl.size() && !mUnitNamesTbl[I].Text.empty())
        {
            mUnits[I].Name = mUnitNamesTbl[I].Text;
        }
        else if (I < mStatTxtTbl.size() && !mStatTxtTbl[I].Text.empty())
        {
            mUnits[I].Name = mStatTxtTbl[I].Text;
        }
        else if (I < sizeof(UnitNames)/sizeof(UnitNames[0]))
        {
            mUnits[I].Name = UnitNames[I];
        }
    }

    return true;
}

bool GameDataLoader::LoadWeaponData(IArchiveReader& Archive)
{
    auto Data = ReadDataFile(Archive, "weapons.dat");
    if (!Data) return false;
    mWeapons = DatDecoder::DecodeWeapons(*Data);
    return true;
}

bool GameDataLoader::LoadFlingyData(IArchiveReader& Archive)
{
    auto Data = ReadDataFile(Archive, "flingy.dat");
    if (!Data) return false;
    mFlingy = DatDecoder::DecodeFlingy(*Data);
    return true;
}

bool GameDataLoader::LoadSpriteData(IArchiveReader& Archive)
{
    auto Data = ReadDataFile(Archive, "sprites.dat");
    if (!Data) return false;
    mSprites = DatDecoder::DecodeSprites(*Data);
    return true;
}

bool GameDataLoader::LoadImageData(IArchiveReader& Archive)
{
    auto Data = ReadDataFile(Archive, "images.dat");
    if (!Data) return false;
    mImages = DatDecoder::DecodeImages(*Data);
    return true;
}

bool GameDataLoader::LoadUpgradeData(IArchiveReader& Archive)
{
    auto Data = ReadDataFile(Archive, "upgrades.dat");
    if (!Data) return false;
    mUpgrades = DatDecoder::DecodeUpgrades(*Data);
    return true;
}

bool GameDataLoader::LoadTechData(IArchiveReader& Archive)
{
    auto Data = ReadDataFile(Archive, "techdata.dat");
    if (!Data) return false;
    mTech = DatDecoder::DecodeTech(*Data);
    return true;
}

bool GameDataLoader::LoadImageTblData(IArchiveReader& Archive)
{
    std::vector<std::string> Paths = {"arr/images.tbl", "images.tbl", "arr/IMAGES.TBL", "IMAGES.TBL"};
    for (auto& P : Paths)
    {
        auto Data = Archive.ReadFile(P);
        if (Data)
        {
            mImageTbl = DatDecoder::DecodeImageTbl(*Data);
            if (!mImageTbl.empty()) return true;
        }
    }
    return false;
}

bool GameDataLoader::LoadImageTblDataRaw(const std::vector<uint8_t>& Data)
{
    if (Data.empty()) return false;
    mImageTbl = DatDecoder::DecodeImageTbl(Data);
    return !mImageTbl.empty();
}

bool GameDataLoader::LoadStatTxtTbl(IArchiveReader& Archive)
{
    std::vector<std::string> Paths = {"stat_txt.tbl", "arr/stat_txt.tbl", "STAT_TXT.TBL", "arr/STAT_TXT.TBL"};
    for (auto& P : Paths)
    {
        auto Data = Archive.ReadFile(P);
        if (Data)
        {
            mStatTxtTbl = TblDecoder::Decode(*Data);
            if (!mStatTxtTbl.empty())
            {
                Core::Utils::Logger::Info("GameDataLoader",
                    "Loaded stat_txt.tbl: " + std::to_string(mStatTxtTbl.size()) + " strings");
                return true;
            }
        }
    }
    Core::Utils::Logger::Warning("GameDataLoader", "stat_txt.tbl not found");
    return false;
}

bool GameDataLoader::LoadUnitNamesTbl(IArchiveReader& Archive)
{
    std::vector<std::string> Paths = {"unitnames.tbl", "arr/unitnames.tbl", "UNITNAMES.TBL", "arr/UNITNAMES.TBL"};
    for (auto& P : Paths)
    {
        auto Data = Archive.ReadFile(P);
        if (Data)
        {
            mUnitNamesTbl = TblDecoder::Decode(*Data);
            if (!mUnitNamesTbl.empty())
            {
                Core::Utils::Logger::Info("GameDataLoader",
                    "Loaded unitnames.tbl: " + std::to_string(mUnitNamesTbl.size()) + " strings");
                return true;
            }
        }
    }
    Core::Utils::Logger::Warning("GameDataLoader", "unitnames.tbl not found");
    return false;
}

bool GameDataLoader::LoadTblDataRaw(const std::vector<uint8_t>& Data)
{
    if (Data.empty()) return false;
    auto Entries = TblDecoder::Decode(Data);
    if (Entries.empty()) return false;
    mExtraTbls.insert(mExtraTbls.end(), Entries.begin(), Entries.end());
    return true;
}

bool GameDataLoader::LoadUnitDataRaw(const std::vector<uint8_t>& Data)
{
    if (Data.empty()) return false;
    mUnits = DatDecoder::DecodeUnits(Data);
    return true;
}

bool GameDataLoader::LoadWeaponDataRaw(const std::vector<uint8_t>& Data)
{
    if (Data.empty()) return false;
    mWeapons = DatDecoder::DecodeWeapons(Data);
    return true;
}

bool GameDataLoader::LoadFlingyDataRaw(const std::vector<uint8_t>& Data)
{
    if (Data.empty()) return false;
    mFlingy = DatDecoder::DecodeFlingy(Data);
    return true;
}

bool GameDataLoader::LoadSpriteDataRaw(const std::vector<uint8_t>& Data)
{
    if (Data.empty()) return false;
    mSprites = DatDecoder::DecodeSprites(Data);
    return true;
}

bool GameDataLoader::LoadImageDataRaw(const std::vector<uint8_t>& Data)
{
    if (Data.empty()) return false;
    mImages = DatDecoder::DecodeImages(Data);
    return true;
}

bool GameDataLoader::LoadUpgradeDataRaw(const std::vector<uint8_t>& Data)
{
    if (Data.empty()) return false;
    mUpgrades = DatDecoder::DecodeUpgrades(Data);
    return true;
}

bool GameDataLoader::LoadTechDataRaw(const std::vector<uint8_t>& Data)
{
    if (Data.empty()) return false;
    mTech = DatDecoder::DecodeTech(Data);
    return true;
}

RawPalette GameDataLoader::LoadUnitPalette(IArchiveReader& Archive)
{
    // Try common palette file paths
    std::vector<std::string> PalPaths = {
        "unit.pal", "arr/unit.pal",
        "tileset/unit.pal", "glu/unit.pal",
    };
    for (auto& Path : PalPaths)
    {
        auto PalData = Archive.ReadFile(Path);
        if (PalData)
        {
            Core::Utils::Logger::Info("GameDataLoader", "Found palette: " + Path);
            auto PalOpt = PalDecoder::DecodePal(*PalData);
            if (PalOpt) return *PalOpt;
        }
    }

    // Try tileset .wpe palettes (OpenBW format: Tileset/<name>.wpe)
    std::vector<std::string> TilesetNames = {
        "badlands", "platform", "install", "ashworld", "jungle", "desert", "ice", "twilight"
    };
    for (auto& Name : TilesetNames)
    {
        std::string Path = "Tileset/" + Name + ".wpe";
        auto WpeData = Archive.ReadFile(Path);
        if (!WpeData) continue;
        auto PalOpt = PalDecoder::DecodeWpe(*WpeData);
        if (PalOpt)
        {
            Core::Utils::Logger::Info("GameDataLoader", "Found tileset palette: " + Path);
            return *PalOpt;
        }
    }

    // Scan MPQ file list for .pal or .wpe
    auto Files = Archive.ListFiles();
    for (auto& F : Files)
    {
        std::string Lower = F.FilePath;
        for (auto& C : Lower) C = (char)tolower(C);
        if (Lower.find(".pal") != std::string::npos ||
            Lower.find(".wpe") != std::string::npos)
        {
            Core::Utils::Logger::Info("GameDataLoader", "Trying: " + F.FilePath);
            auto PalData = Archive.ReadFile(F.FilePath);
            if (!PalData) continue;
            auto PalOpt = PalDecoder::DecodePal(*PalData);
            if (!PalOpt && Lower.find(".wpe") != std::string::npos)
                PalOpt = PalDecoder::DecodeWpe(*PalData);
            if (PalOpt) return *PalOpt;
        }
    }

    Core::Utils::Logger::Info("GameDataLoader", "No palette found, using default");
    return PalDecoder::GetDefaultPalette();
}

const UnitEntry* GameDataLoader::FindUnitByID(uint32_t ID) const
{
    if (ID < mUnits.size())
        return &mUnits[ID];
    return nullptr;
}

const WeaponEntry* GameDataLoader::FindWeaponByID(uint32_t ID) const
{
    if (ID < mWeapons.size())
        return &mWeapons[ID];
    return nullptr;
}

const SpriteEntry* GameDataLoader::FindSpriteByID(uint32_t ID) const
{
    if (ID < mSprites.size())
        return &mSprites[ID];
    return nullptr;
}

const std::string* GameDataLoader::FindStatTxtString(uint32_t ID) const
{
    if (ID < mStatTxtTbl.size())
        return &mStatTxtTbl[ID].Text;
    return nullptr;
}

const std::string* GameDataLoader::FindUnitName(uint32_t ID) const
{
    if (ID < mUnitNamesTbl.size() && !mUnitNamesTbl[ID].Text.empty())
        return &mUnitNamesTbl[ID].Text;
    if (ID < mUnits.size() && !mUnits[ID].Name.empty())
        return &mUnits[ID].Name;
    return nullptr;
}

struct ChkUnitGrpMapping
{
    int ChkUnitID;
    const char* GrpPath;
};

static constexpr ChkUnitGrpMapping kChkUnitGrpMappings[] = {
    {0x00, "unit/terran/marine.grp"},
    {0x01, "unit/terran/ghost.grp"},
    {0x02, "unit/terran/vulture.grp"},
    {0x03, "unit/terran/goliath.grp"},
    {0x05, "unit/terran/tank.grp"},
    {0x06, "unit/terran/stank.grp"},
    {0x07, "unit/terran/scv.grp"},
    {0x0A, "unit/terran/bomber.grp"},
    {0x0B, "unit/terran/wessel.grp"},
    {0x0C, "unit/terran/dropship.grp"},
    {0x0D, "unit/terran/battlecr.grp"},
    {0x14, "unit/terran/firebat.grp"},
    {0x15, "unit/terran/medic.grp"},
    {0x3A, "unit/terran/phoenix.grp"},
    {0x64, "unit/neutral/min01.grp"},
    {0x65, "unit/neutral/min02.grp"},
    {0x66, "unit/neutral/min03.grp"},
    {0x67, "unit/neutral/geyser.grp"},
    {0x6E, "unit/terran/control.grp"},
    {0x6F, "unit/terran/comsat.grp"},
    {0x70, "unit/terran/nukesilo.grp"},
    {0x71, "unit/terran/depot.grp"},
    {0x72, "unit/terran/refinery.grp"},
    {0x73, "unit/terran/tbarrack.grp"},
    {0x74, "unit/terran/academy.grp"},
    {0x75, "unit/terran/factory.grp"},
    {0x76, "unit/terran/starport.grp"},
    {0x77, "unit/terran/controlt.grp"},
    {0x78, "unit/terran/drydocks.grp"},
    {0x79, "unit/terran/chemlab.grp"},
    {0x7A, "unit/terran/physics.grp"},
    {0x7B, "unit/terran/machines.grp"},
    {0x7C, "unit/terran/research.grp"},
    {0x7D, "unit/terran/weaponpl.grp"},
    {0x82, "unit/terran/missile.grp"},
    {0x83, "unit/terran/pillbox.grp"},
    {0xB0, "unit/neutral/min01.grp"},
    {0xB1, "unit/neutral/min02.grp"},
    {0xB2, "unit/neutral/min03.grp"},
    {0xBC, "unit/neutral/geyser.grp"},
};

static std::string ChkUnitToGrpPath(int ChkUnitID)
{
    for (const auto& Mapping : kChkUnitGrpMappings)
    {
        if (Mapping.ChkUnitID == ChkUnitID)
            return Mapping.GrpPath;
    }
    return {};
}

std::string GameDataLoader::ResolveGrpPath(uint32_t ImageID) const
{
    return ChkUnitToGrpPath((int)ImageID);
}

void GameDataLoader::RebuildImageMappingFromTbl()
{
    Core::Utils::Logger::Info("GameDataLoader",
        "Entity GRP mapping uses explicit CHK-unit table; images.tbl remap disabled");
}

} // namespace Resource

