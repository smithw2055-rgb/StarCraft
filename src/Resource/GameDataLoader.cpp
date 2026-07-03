#include "Resource/GameDataLoader.h"
#include "Resource/TblDecoder.h"
#include "Core/Utils/Logger.h"

#include <algorithm>
#include <cstdint>

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
    LoadImageRelationData(*Archive);
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

bool GameDataLoader::LoadImageRelationData(IArchiveReader& Archive)
{
    auto Data = ReadDataFile(Archive, "images.rel");
    if (!Data) return false;
    mImageRelations = DatDecoder::DecodeImageRelations(*Data);
    return !mImageRelations.empty();
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

bool GameDataLoader::LoadImageRelationDataRaw(const std::vector<uint8_t>& Data)
{
    if (Data.empty()) return false;
    mImageRelations = DatDecoder::DecodeImageRelations(Data);
    return !mImageRelations.empty();
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

const ImageEntry* GameDataLoader::FindImageByID(uint32_t ID) const
{
    if (ID < mImages.size())
        return &mImages[ID];
    return nullptr;
}

const ImageRelationEntry* GameDataLoader::FindImageRelationByID(uint32_t ID) const
{
    if (ID < mImageRelations.size())
        return &mImageRelations[ID];
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
    {0x04, "unit/terran/goliatht.grp"},
    {0x05, "unit/terran/tank.grp"},
    {0x06, "unit/terran/tankt.grp"},
    {0x07, "unit/terran/scv.grp"},
    {0x08, "unit/terran/bomber.grp"},
    {0x09, "unit/terran/wessel.grp"},
    {0x0A, "unit/terran/firebat.grp"},
    {0x0B, "unit/terran/dropship.grp"},
    {0x0C, "unit/terran/battlecr.grp"},
    {0x0D, "unit/terran/spider.grp"},
    {0x0E, "unit/terran/nukemiss.grp"},
    {0x0F, "unit/neutral/civilian.grp"},
    {0x10, "unit/terran/ughost.grp"},
    {0x11, "unit/terran/goliath.grp"},
    {0x12, "unit/terran/goliatht.grp"},
    {0x13, "unit/terran/vulture.grp"},
    {0x14, "unit/terran/marine.grp"},
    {0x15, "unit/terran/bomber.grp"},
    {0x16, "unit/terran/wessel.grp"},
    {0x17, "unit/terran/tank.grp"},
    {0x18, "unit/terran/tankt.grp"},
    {0x19, "unit/terran/stank.grp"},
    {0x1A, "unit/terran/stankt.grp"},
    {0x1B, "unit/neutral/civilian.grp"},
    {0x1C, "unit/terran/battlecr.grp"},
    {0x1D, "unit/terran/battlecr.grp"},
    {0x1E, "unit/terran/stank.grp"},
    {0x1F, "unit/terran/stankt.grp"},
    {0x20, "unit/terran/firebat.grp"},
    {0x22, "unit/terran/medic.grp"},
    {0x23, "unit/zerg/larva.grp"},
    {0x24, "unit/zerg/egg.grp"},
    {0x25, "unit/zerg/zergling.grp"},
    {0x26, "unit/zerg/hydra.grp"},
    {0x27, "unit/zerg/ultra.grp"},
    {0x28, "unit/zerg/brood.grp"},
    {0x29, "unit/zerg/drone.grp"},
    {0x2A, "unit/zerg/overlord.grp"},
    {0x2B, "unit/zerg/mutalid.grp"},
    {0x2C, "unit/zerg/guardian.grp"},
    {0x2D, "unit/zerg/queen.grp"},
    {0x2E, "unit/zerg/defiler.grp"},
    {0x2F, "unit/zerg/avenger.grp"},
    {0x30, "unit/zerg/ultra.grp"},
    {0x31, "unit/zerg/queen.grp"},
    {0x32, "unit/zerg/bugguy.grp"},
    {0x33, "unit/zerg/uikerr.grp"},
    {0x34, "unit/zerg/defiler.grp"},
    {0x35, "unit/zerg/hydra.grp"},
    {0x36, "unit/zerg/zergling.grp"},
    {0x37, "unit/zerg/mutalid.grp"},
    {0x38, "unit/zerg/guardian.grp"},
    {0x39, "unit/zerg/overlord.grp"},
    {0x3A, "unit/terran/phoenix.grp"},
    {0x3B, "unit/zerg/cocoon.grp"},
    {0x3C, "unit/protoss/corsair.grp"},
    {0x3D, "unit/protoss/dtemplar.grp"},
    {0x3E, "unit/zerg/devour.grp"},
    {0x3F, "unit/protoss/darchn.grp"},
    {0x40, "unit/protoss/probe.grp"},
    {0x41, "unit/protoss/zealot.grp"},
    {0x42, "unit/protoss/dragoon.grp"},
    {0x43, "unit/protoss/templar.grp"},
    {0x44, "unit/protoss/archon.grp"},
    {0x45, "unit/protoss/shuttle.grp"},
    {0x46, "unit/protoss/scout.grp"},
    {0x47, "unit/protoss/arbiter.grp"},
    {0x48, "unit/protoss/carrier.grp"},
    {0x49, "unit/protoss/intercep.grp"},
    {0x4A, "unit/protoss/dtemplar.grp"},
    {0x4B, "unit/protoss/dtemplar.grp"},
    {0x4C, "unit/protoss/archon.grp"},
    {0x4D, "unit/protoss/zealot.grp"},
    {0x4E, "unit/protoss/dragoon.grp"},
    {0x4F, "unit/protoss/templar.grp"},
    {0x50, "unit/protoss/scout.grp"},
    {0x51, "unit/protoss/trilob.grp"},
    {0x52, "unit/protoss/carrier.grp"},
    {0x53, "unit/protoss/trilob.grp"},
    {0x54, "unit/protoss/witness.grp"},
    {0x55, "unit/protoss/sapper.grp"},
    {0x56, "unit/protoss/arbiter.grp"},
    {0x57, "unit/protoss/templar.grp"},
    {0x58, "unit/protoss/scout.grp"},
    {0x59, "unit/neutral/bcritter.grp"},
    {0x5A, "unit/neutral/jcritter.grp"},
    {0x5B, "unit/terran/dropship.grp"},
    {0x5C, "unit/terran/bomber.grp"},
    {0x5D, "unit/neutral/SCritter.grp"},
    {0x5E, "unit/neutral/KCritter.grp"},
    {0x5F, "unit/neutral/acritter.grp"},
    {0x60, "unit/neutral/ICritter.grp"},
    {0x61, "unit/zerg/LurkEgg.grp"},
    {0x62, "unit/protoss/corsair.grp"},
    {0x63, "unit/terran/ghost.grp"},
    {0x64, "unit/terran/ghost.grp"},
    {0x65, "unit/neutral/maprev.grp"},
    {0x66, "unit/terran/battlecr.grp"},
    {0x67, "unit/zerg/lurker.grp"},
    {0x68, "unit/zerg/uikerr.grp"},
    {0x69, "unit/thingy/disrupt.grp"},
    {0x6A, "unit/terran/control.grp"},
    {0x6B, "unit/terran/comsat.grp"},
    {0x6C, "unit/terran/nukesilo.grp"},
    {0x6D, "unit/terran/depot.grp"},
    {0x6E, "unit/terran/refinery.grp"},
    {0x6F, "unit/terran/tbarrack.grp"},
    {0x70, "unit/terran/academy.grp"},
    {0x71, "unit/terran/factory.grp"},
    {0x72, "unit/terran/starport.grp"},
    {0x73, "unit/terran/controlt.grp"},
    {0x74, "unit/terran/drydocks.grp"},
    {0x75, "unit/terran/chemlab.grp"},
    {0x76, "unit/terran/physics.grp"},
    {0x78, "unit/terran/machines.grp"},
    {0x7A, "unit/terran/research.grp"},
    {0x7B, "unit/terran/weaponpl.grp"},
    {0x7C, "unit/terran/missile.grp"},
    {0x7D, "unit/terran/pillbox.grp"},
    {0x7E, "unit/neutral/cbattle.grp"},
    {0x7F, "unit/neutral/ion.grp"},
    {0x80, "unit/neutral/Uraj.grp"},
    {0x81, "unit/neutral/Khalis.grp"},
    {0x82, "unit/zerg/infest03.grp"},
    {0x83, "unit/zerg/hatchery.grp"},
    {0x84, "unit/zerg/lair.grp"},
    {0x85, "unit/zerg/hive.grp"},
    {0x86, "unit/zerg/nyduspit.grp"},
    {0x87, "unit/zerg/snakey.grp"},
    {0x88, "unit/zerg/mutapit.grp"},
    {0x89, "unit/zerg/spire.grp"},
    {0x8A, "unit/zerg/nest.grp"},
    {0x8B, "unit/zerg/mutacham.grp"},
    {0x8C, "unit/zerg/ultra.grp"},
    {0x8D, "unit/zerg/spire.grp"},
    {0x8E, "unit/zerg/zspawn01.grp"},
    {0x8F, "unit/zerg/zspawn02.grp"},
    {0x90, "unit/zerg/fcolony.grp"},
    {0x92, "unit/zerg/scolony.grp"},
    {0x93, "unit/zerg/over1.grp"},
    {0x94, "unit/zerg/over2.grp"},
    {0x95, "unit/zerg/extract.grp"},
    {0x96, "unit/neutral/kerrchry.grp"},
    {0x97, "unit/zerg/cerebrat.grp"},
    {0x98, "unit/zerg/ucereb.grp"},
    {0x9A, "unit/protoss/nexus.grp"},
    {0x9B, "unit/protoss/robotic.grp"},
    {0x9C, "unit/protoss/pylon.grp"},
    {0x9D, "unit/protoss/assim.grp"},
    {0x9F, "unit/protoss/prism.grp"},
    {0xA0, "unit/protoss/gateway.grp"},
    {0xA2, "unit/protoss/photon.grp"},
    {0xA3, "unit/protoss/citadel.grp"},
    {0xA4, "unit/protoss/gencore.grp"},
    {0xA5, "unit/protoss/archives.grp"},
    {0xA6, "unit/protoss/forge.grp"},
    {0xA7, "unit/protoss/stargate.grp"},
    {0xA8, "unit/protoss/stasis.grp"},
    {0xA9, "unit/protoss/prism.grp"},
    {0xAA, "unit/protoss/warp.grp"},
    {0xAB, "unit/protoss/robotic.grp"},
    {0xAC, "unit/protoss/sbattery.grp"},
    {0xAD, "unit/neutral/khyad01.grp"},
    {0xAE, "unit/neutral/temple.grp"},
    {0xAF, "unit/protoss/XelTempl.grp"},
    {0xB0, "unit/neutral/min01.grp"},
    {0xB1, "unit/neutral/min02.grp"},
    {0xB2, "unit/neutral/min03.grp"},
    {0xB8, "unit/neutral/starbase.grp"},
    {0xBC, "unit/neutral/geyser.grp"},
    {0xBD, "unit/protoss/XWarpGat.grp"},
    {0xBE, "unit/neutral/PsiDisr.grp"},
    {0xC2, "unit/protoss/beacon.grp"},
    {0xC3, "unit/protoss/beacon.grp"},
    {0xC4, "unit/protoss/beacon.grp"},
    {0xC5, "unit/protoss/beacon.grp"},
    {0xC6, "unit/protoss/beacon.grp"},
    {0xC7, "unit/protoss/beacon.grp"},
    {0xC8, "unit/neutral/Generate.grp"},
    {0xC9, "unit/zerg/cocoon.grp"},
    {0xCA, "unit/thingy/dswarm.grp"},
    {0xCB, "unit/neutral/tgnShad.grp"},
    {0xCC, "unit/neutral/tgnShad.grp"},
    {0xCD, "unit/neutral/tgnShad.grp"},
    {0xCE, "unit/neutral/tgnShad.grp"},
    {0xCF, "unit/neutral/tgnShad.grp"},
    {0xD0, "unit/neutral/tgnShad.grp"},
    {0xD1, "unit/neutral/tgnShad.grp"},
    {0xD2, "unit/neutral/tgnShad.grp"},
    {0xD3, "unit/neutral/tgnShad.grp"},
    {0xD4, "unit/neutral/tgnShad.grp"},
    {0xD5, "unit/neutral/tgnShad.grp"},
    {0xD6, "unit/thingy/startloc.grp"},
    {0xD7, "unit/neutral/flag.grp"},
    {0xD8, "unit/neutral/kerregg.grp"},
    {0xD9, "unit/neutral/psiemit.grp"},
    {0xDA, "unit/neutral/datadisk.grp"},
    {0xDB, "unit/neutral/khchunk.grp"},
    {0xDC, "unit/neutral/orechunk.grp"},
    {0xDD, "unit/neutral/orechunk.grp"},
    {0xDE, "unit/neutral/gasorb.grp"},
    {0xDF, "unit/neutral/gasorb.grp"},
    {0xE0, "unit/neutral/gassac.grp"},
    {0xE1, "unit/neutral/gassac.grp"},
    {0xE2, "unit/neutral/gastank.grp"},
    {0xE3, "unit/neutral/gastank.grp"},
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

static std::string NormalizeGrpPath(std::string Path)
{
    std::replace(Path.begin(), Path.end(), '\\', '/');
    while (!Path.empty() && (Path.front() == '/' || Path.front() == '\\'))
        Path.erase(Path.begin());
    return Path;
}

std::string GameDataLoader::ResolveGrpPath(uint32_t ImageID) const
{
    std::string LegacyPath = ChkUnitToGrpPath((int)ImageID);
    if (!LegacyPath.empty())
        return LegacyPath;

    if (ImageID < mImages.size())
    {
        const uint32_t TblIndex = mImages[(size_t)ImageID].GrpFilenameIndex;
        if (TblIndex > 0 && TblIndex <= mImageTbl.size())
        {
            std::string Path = NormalizeGrpPath(mImageTbl[(size_t)TblIndex - 1].Filename);
            if (!Path.empty())
                return Path;
        }
    }

    return {};
}

uint32_t GameDataLoader::ResolveUnitImageID(uint32_t ChkUnitID) const
{
    if (ChkUnitID >= mUnits.size())
        return UINT32_MAX;

    const uint32_t SpriteID = mUnits[(size_t)ChkUnitID].SpriteID;
    if (SpriteID >= mSprites.size())
        return UINT32_MAX;

    const uint32_t ImageID = mSprites[(size_t)SpriteID].ImageID;
    if (ImageID >= mImages.size())
        return UINT32_MAX;

    if (ResolveGrpPath(ImageID).empty())
        return UINT32_MAX;

    return ImageID;
}

void GameDataLoader::RebuildImageMappingFromTbl()
{
    Core::Utils::Logger::Info("GameDataLoader",
        "Entity GRP mapping uses images.dat/images.tbl with CHK-unit fallback markers");
    if (!mImageRelations.empty())
    {
        size_t RootCount = 0;
        size_t ShadowCount = 0;
        size_t DeathCount = 0;
        size_t GlowCount = 0;
        size_t LinkedCount = 0;
        for (const auto& Relation : mImageRelations)
        {
            if (Relation.IsRootOrStandalone()) ++RootCount;
            else if (Relation.IsShadow()) ++ShadowCount;
            else if (Relation.IsDeathOrTransition()) ++DeathCount;
            else if (Relation.IsGlowOverlay()) ++GlowCount;
            else if (Relation.IsLinkedVariant()) ++LinkedCount;
        }

        Core::Utils::Logger::Info("GameDataLoader",
            "Loaded images.rel relations: total=" + std::to_string(mImageRelations.size()) +
            " root=" + std::to_string(RootCount) +
            " shadow=" + std::to_string(ShadowCount) +
            " death=" + std::to_string(DeathCount) +
            " glow=" + std::to_string(GlowCount) +
            " linked=" + std::to_string(LinkedCount));
    }
}

} // namespace Resource

