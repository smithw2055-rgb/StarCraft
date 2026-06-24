#pragma once

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

#include "Resource/IArchiveReader.h"
#include "Resource/DatDecoder.h"
#include "Resource/PalDecoder.h"
#include "Resource/TblDecoder.h"

namespace Resource
{

class GameDataLoader
{
public:
    GameDataLoader();
    ~GameDataLoader();

    bool LoadFromInstall(const std::string& InstallPath);

    // Load individual data files from an open archive
    bool LoadUnitData(IArchiveReader& Archive);
    bool LoadWeaponData(IArchiveReader& Archive);
    bool LoadFlingyData(IArchiveReader& Archive);
    bool LoadSpriteData(IArchiveReader& Archive);
    bool LoadImageData(IArchiveReader& Archive);
    bool LoadUpgradeData(IArchiveReader& Archive);
    bool LoadTechData(IArchiveReader& Archive);
    bool LoadImageTblData(IArchiveReader& Archive);

    // TBL string tables
    bool LoadStatTxtTbl(IArchiveReader& Archive);
    bool LoadUnitNamesTbl(IArchiveReader& Archive);

    // Load from raw data (already read from archive)
    bool LoadUnitDataRaw(const std::vector<uint8_t>& Data);
    bool LoadWeaponDataRaw(const std::vector<uint8_t>& Data);
    bool LoadFlingyDataRaw(const std::vector<uint8_t>& Data);
    bool LoadSpriteDataRaw(const std::vector<uint8_t>& Data);
    bool LoadImageDataRaw(const std::vector<uint8_t>& Data);
    bool LoadImageTblDataRaw(const std::vector<uint8_t>& Data);
    bool LoadUpgradeDataRaw(const std::vector<uint8_t>& Data);
    bool LoadTechDataRaw(const std::vector<uint8_t>& Data);
    bool LoadTblDataRaw(const std::vector<uint8_t>& Data);

    // Palette loading (returns raw palette)
    RawPalette LoadUnitPalette(IArchiveReader& Archive);
    const RawPalette& GetUnitPalette() const { return mUnitPalette; }
    bool IsLoaded() const { return mIsLoaded; }

    const UnitEntry* FindUnitByID(uint32_t ID) const;
    const WeaponEntry* FindWeaponByID(uint32_t ID) const;
    const SpriteEntry* FindSpriteByID(uint32_t ID) const;

    const std::string* FindStatTxtString(uint32_t ID) const;
    const std::string* FindUnitName(uint32_t ID) const;

    // Resolve entity GRP path from CHK unit IDs.
    std::string ResolveGrpPath(uint32_t ImageID) const;

    // Kept for compatibility with existing init flow; entity art now uses the
    // explicit CHK-unit mapping below instead of images.tbl indirection.
    void RebuildImageMappingFromTbl();

private:
    bool mIsLoaded = false;

    std::vector<UnitEntry> mUnits;
    std::vector<WeaponEntry> mWeapons;
    std::vector<FlingyEntry> mFlingy;
    std::vector<SpriteEntry> mSprites;
    std::vector<ImageEntry> mImages;
    std::vector<ImageTblEntry> mImageTbl;
    std::vector<UpgradeEntry> mUpgrades;
    std::vector<TechEntry> mTech;

    RawPalette mUnitPalette;

    // TBL string tables
    std::vector<TblEntry> mStatTxtTbl;
    std::vector<TblEntry> mUnitNamesTbl;
    std::vector<TblEntry> mExtraTbls;
};

} // namespace Resource
