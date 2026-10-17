#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Game::Map
{

// ── CHK section ──────────────────────────────────────────
struct ChkSectionInfo
{
    char Name[4];
    uint32_t Size;
    size_t Offset;
};

// ── Player info ──────────────────────────────────────────
struct PlayerInfo
{
    uint8_t Owner = 7;    // 0=Inactive..7=Neutral
    uint8_t Race = 5;     // 0=Zerg..7=Inactive
    uint8_t Force = 0;
    uint8_t Color = 0;
};

// ── Force info ───────────────────────────────────────────
struct ForceInfo
{
    uint16_t NameStringIndex = 0;
    uint8_t Flags = 0;  // bit0=random, bit1=allies, bit2=alliedVictory, bit3=sharedVision
};

// ── UNIT: placed units (36 bytes each) ───────────────────
struct PlacedUnit
{
    uint16_t X;          // pixel coordinate
    uint16_t Y;          // pixel coordinate
    uint16_t UnitID;
    uint16_t RelationFlags;   // bit9=nydus, bit10=addon
    uint16_t ValidFlags;      // bit0=cloak, bit1=burrow, bit2=transit, bit3=hallu, bit4=invincible
    uint16_t ChangeFlags;     // bit0=owner, bit1=hp, bit2=shield, bit3=energy, bit4=resource, bit5=hangar
    uint8_t Owner;
    uint8_t HpPercent;
    uint8_t ShieldPercent;
    uint8_t EnergyPercent;
    uint32_t ResourceAmount;
    uint16_t HangarCount;
    uint16_t StateFlags;      // bit0=cloaked, bit1=burrowed, bit2=transit, bit3=hallu, bit4=invincible
};

// ── Placed doodad / sprite (THG2 / THG: 10 bytes each) ───
struct PlacedDoodad
{
    uint16_t ID = 0;
    uint16_t X = 0;
    uint16_t Y = 0;
    uint8_t Owner = 11;
    uint8_t Flags = 0;
    bool IsSprite = false;
};

// ── String table ─────────────────────────────────────────
struct StringTable
{
    std::vector<std::string> Strings;
};

// ── Start location ──────────────────────────────────────
struct StartLocation
{
    uint16_t X;  // pixel coordinate
    uint16_t Y;  // pixel coordinate
    uint8_t Owner;
};

// ── ChkParser ────────────────────────────────────────────
class ChkParser
{
public:
    ChkParser() = default;

    bool Parse(const uint8_t* Data, size_t DataSize);

    // Getters
    uint16_t GetTileset() const { return mTileset; }
    uint16_t GetMapWidth() const { return mMapWidth; }
    uint16_t GetMapHeight() const { return mMapHeight; }
    uint16_t GetFormatVersion() const { return mFormatVersion; }

    const PlayerInfo& GetPlayer(int Index) const { return mPlayers[Index]; }
    const PlayerInfo* GetPlayers() const { return mPlayers; }

    const ForceInfo& GetForce(int Index) const { return mForces[Index]; }

    uint32_t GetMapTypeFlag() const { return mMapTypeFlag; }

    const PlacedUnit* GetUnits() const { return mUnits.data(); }
    size_t GetUnitCount() const { return mUnits.size(); }

    const std::vector<PlacedDoodad>& GetDoodads() const { return mDoodads; }
    size_t GetDoodadCount() const { return mDoodads.size(); }

    const StringTable& GetStringTable() const { return mStringTable; }

    // Raw megatile terrain data
    const std::vector<uint16_t>& GetMegaTiles() const { return mMegaTiles; }

    const std::vector<StartLocation>& GetStartLocations() const { return mStartLocations; }

    // SPRP: scenario name & description string indices
    uint16_t GetScenarioNameStringIndex() const { return mNameStrIndex; }
    uint16_t GetScenarioDescStringIndex() const { return mDescStrIndex; }

    // All sections found
    const std::vector<ChkSectionInfo>& GetSections() const { return mSections; }
    const uint8_t* GetRawData() const { return mRawData; }

private:
    std::vector<ChkSectionInfo> CollectSections();

    void ParseTYPE(const ChkSectionInfo& S);
    void ParseVER(const ChkSectionInfo& S);
    void ParseERA(const ChkSectionInfo& S);
    void ParseDIM(const ChkSectionInfo& S);
    void ParseOWNR(const ChkSectionInfo& S);
    void ParseSIDE(const ChkSectionInfo& S);
    void ParseFORC(const ChkSectionInfo& S);
    void ParseCOLR(const ChkSectionInfo& S);
    void ParseSPRP(const ChkSectionInfo& S);
    void ParseMTXM(const ChkSectionInfo& S);
    void ParseSTR(const ChkSectionInfo& S);
    void ParseUNIT(const ChkSectionInfo& S);
    void ParseTHG2(const ChkSectionInfo& S);

    const uint8_t* mRawData = nullptr;
    size_t mRawSize = 0;

    uint16_t mTileset = 0;
    uint16_t mMapWidth = 0;
    uint16_t mMapHeight = 0;
    uint16_t mFormatVersion = 0;
    uint32_t mMapTypeFlag = 0;

    PlayerInfo mPlayers[12];
    ForceInfo mForces[4];
    std::vector<PlacedUnit> mUnits;
    std::vector<PlacedDoodad> mDoodads;
    std::vector<StartLocation> mStartLocations;
    StringTable mStringTable;
    std::vector<uint16_t> mMegaTiles;

    uint16_t mNameStrIndex = 0;
    uint16_t mDescStrIndex = 0;

    std::vector<ChkSectionInfo> mSections;
};

} // namespace Game::Map
