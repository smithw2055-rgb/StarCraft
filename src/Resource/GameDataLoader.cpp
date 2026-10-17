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
    LoadStatTxtTbl(*Archive);
    LoadUnitNamesTbl(*Archive);
    AllOk = LoadUnitData(*Archive) && AllOk;
    AllOk = LoadWeaponData(*Archive) && AllOk;
    AllOk = LoadFlingyData(*Archive) && AllOk;
    AllOk = LoadSpriteData(*Archive) && AllOk;
    AllOk = LoadImageData(*Archive) && AllOk;
    LoadImageRelationData(*Archive);
    AllOk = LoadUpgradeData(*Archive) && AllOk;
    AllOk = LoadTechData(*Archive) && AllOk;
    LoadOrderData(*Archive);
    LoadImageTblData(*Archive);
    mUnitPalette = LoadUnitPalette(*Archive);

    Archive->Close();

    mIsLoaded = AllOk;
    Core::Utils::Logger::Info("GameDataLoader",
        std::string("Game data loaded: ") + (AllOk ? "OK" : "Partial"));
    return AllOk;
}

void GameDataLoader::ApplyUnitNames()
{
    for (size_t I = 0; I < mUnits.size(); ++I)
    {
        if (I < mUnitNamesTbl.size() && !mUnitNamesTbl[I].Text.empty())
        {
            mUnits[I].Name = mUnitNamesTbl[I].Text;
        }
        else if (I < mStatTxtTbl.size() && !mStatTxtTbl[I].Text.empty())
        {
            mUnits[I].Name = mStatTxtTbl[I].Text;
        }
    }
}

bool GameDataLoader::LoadUnitData(IArchiveReader& Archive)
{
    auto Data = ReadDataFile(Archive, "units.dat");
    if (!Data) return false;
    mUnits = DatDecoder::DecodeUnits(*Data);
    ApplyUnitNames();
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

bool GameDataLoader::LoadOrderData(IArchiveReader& Archive)
{
    auto Data = ReadDataFile(Archive, "orders.dat");
    if (!Data) return false;
    mOrders = DatDecoder::DecodeOrders(*Data);
    if (!mOrders.empty())
    {
        Core::Utils::Logger::Info("GameDataLoader",
            "Loaded orders.dat: " + std::to_string(mOrders.size()) + " entries");
        return true;
    }
    return false;
}

bool GameDataLoader::LoadImageTblDataRaw(const std::vector<uint8_t>& Data)
{
    if (Data.empty()) return false;
    mImageTbl = DatDecoder::DecodeImageTbl(Data);
    return !mImageTbl.empty();
}

bool GameDataLoader::LoadOrderDataRaw(const std::vector<uint8_t>& Data)
{
    if (Data.empty()) return false;
    mOrders = DatDecoder::DecodeOrders(Data);
    return !mOrders.empty();
}

bool GameDataLoader::LoadStatTxtTbl(IArchiveReader& Archive)
{
    std::vector<std::string> Paths = {
        "rez/stat_txt.tbl", "stat_txt.tbl", "arr/stat_txt.tbl",
        "rez/STAT_TXT.TBL", "STAT_TXT.TBL", "arr/STAT_TXT.TBL"
    };
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
                ApplyUnitNames();
                return true;
            }
        }
    }
    Core::Utils::Logger::Warning("GameDataLoader", "stat_txt.tbl not found");
    return false;
}

bool GameDataLoader::LoadUnitNamesTbl(IArchiveReader& Archive)
{
    std::vector<std::string> Paths = {
        "rez/unitnames.tbl", "unitnames.tbl", "arr/unitnames.tbl",
        "rez/UNITNAMES.TBL", "UNITNAMES.TBL", "arr/UNITNAMES.TBL"
    };
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
                ApplyUnitNames();
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
    ApplyUnitNames();
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

const FlingyEntry* GameDataLoader::FindFlingyByID(uint32_t ID) const
{
    if (ID < mFlingy.size())
        return &mFlingy[ID];
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

const OrderEntry* GameDataLoader::FindOrderByID(uint32_t ID) const
{
    if (ID < mOrders.size())
        return &mOrders[ID];
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

static std::string NormalizeGrpPath(std::string Path)
{
    std::replace(Path.begin(), Path.end(), '\\', '/');
    while (!Path.empty() && (Path.front() == '/' || Path.front() == '\\'))
        Path.erase(Path.begin());
    return Path;
}

std::string GameDataLoader::ResolveGrpPath(uint32_t ImageID) const
{
    return ResolveImageGrpPath(ImageID);
}

std::string GameDataLoader::ResolveImageGrpPath(uint32_t ImageID) const
{
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

std::string GameDataLoader::ResolveImageOverlayPath(uint32_t ImageID, size_t LoType) const
{
    if (ImageID >= mImages.size())
        return {};

    const auto& Image = mImages[(size_t)ImageID];
    uint32_t TblIndex = 0;
    switch (LoType)
    {
        case 0: TblIndex = Image.AttackFilenameIndex; break;
        case 1: TblIndex = Image.DamageFilenameIndex; break;
        case 2: TblIndex = Image.SpecialFilenameIndex; break;
        case 3: TblIndex = Image.LandingDustFilenameIndex; break;
        case 4: TblIndex = Image.LiftOffFilenameIndex; break;
        case 5: TblIndex = Image.ShieldFilenameIndex; break;
        default: return {};
    }

    if (TblIndex == 0 || TblIndex > mImageTbl.size())
        return {};
    return NormalizeGrpPath(mImageTbl[(size_t)TblIndex - 1].Filename);
}

uint32_t GameDataLoader::ResolveUnitSpriteID(uint32_t ChkUnitID) const
{
    if (ChkUnitID >= mUnits.size())
        return UINT32_MAX;

    const uint32_t FlingyID = mUnits[(size_t)ChkUnitID].FlingyID;
    if (FlingyID >= mFlingy.size())
        return UINT32_MAX;

    const uint32_t SpriteID = mFlingy[(size_t)FlingyID].SpriteID;
    if (SpriteID >= mSprites.size())
        return UINT32_MAX;

    return SpriteID;
}

uint32_t GameDataLoader::ResolveUnitImageID(uint32_t ChkUnitID) const
{
    const uint32_t SpriteID = ResolveUnitSpriteID(ChkUnitID);
    if (SpriteID == UINT32_MAX)
        return UINT32_MAX;

    const uint32_t ImageID = mSprites[(size_t)SpriteID].ImageID;
    if (ImageID >= mImages.size())
        return UINT32_MAX;

    if (ResolveImageGrpPath(ImageID).empty())
        return UINT32_MAX;

    return ImageID;
}

uint32_t GameDataLoader::ResolveUnitConstructionImageID(uint32_t ChkUnitID) const
{
    if (ChkUnitID >= mUnits.size())
        return UINT32_MAX;

    const uint32_t ImageID = mUnits[(size_t)ChkUnitID].ConstructionImageID;
    // Brood War uses construction image id 0 as a sentinel for buildings
    // without a separate construction GRP (notably Terran Refinery and
    // Protoss Assimilator). OpenBW's set_construction_graphic falls back to
    // the unit's normal sprite image in this case. Treating 0 as a real image
    // renders the SCV graphic inside the building footprint instead.
    if (ImageID == 0)
        return ResolveUnitImageID(ChkUnitID);
    if (ImageID == UINT32_MAX || ImageID >= mImages.size())
        return UINT32_MAX;

    if (ResolveImageGrpPath(ImageID).empty())
        return UINT32_MAX;

    return ImageID;
}

uint32_t GameDataLoader::ResolveUnitSubunitImageID(uint32_t ChkUnitID) const
{
    if (ChkUnitID >= mUnits.size())
        return UINT32_MAX;

    const uint32_t SubunitID = mUnits[(size_t)ChkUnitID].Subunit1ID;
    if (SubunitID >= mUnits.size())
        return UINT32_MAX;

    return ResolveUnitImageID(SubunitID);
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

