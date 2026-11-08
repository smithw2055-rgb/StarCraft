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

// === Inlined from src/Game/Map/ChkParser.cpp ===


#include "Game/Locale/TextEncoding.h"

#include <cstring>
#include <cstdio>
#include <algorithm>

namespace Game::Map
{

inline bool ChkParser::Parse(const uint8_t* Data, size_t DataSize)
{
    if (!Data || DataSize < 8)
        return false;

    mRawData = Data;
    mRawSize = DataSize;
    mSections = CollectSections();

    for (auto& S : mSections)
    {
        if (memcmp(S.Name, "TYPE", 4) == 0) ParseTYPE(S);
        else if (memcmp(S.Name, "VER ", 4) == 0) ParseVER(S);
        else if (memcmp(S.Name, "ERA ", 4) == 0) ParseERA(S);
        else if (memcmp(S.Name, "DIM ", 4) == 0) ParseDIM(S);
        else if (memcmp(S.Name, "OWNR", 4) == 0) ParseOWNR(S);
        else if (memcmp(S.Name, "SIDE", 4) == 0) ParseSIDE(S);
        else if (memcmp(S.Name, "FORC", 4) == 0) ParseFORC(S);
        else if (memcmp(S.Name, "COLR", 4) == 0) ParseCOLR(S);
        else if (memcmp(S.Name, "SPRP", 4) == 0) ParseSPRP(S);
        else if (memcmp(S.Name, "MTXM", 4) == 0) ParseMTXM(S);
        else if (memcmp(S.Name, "STR ", 4) == 0) ParseSTR(S);
        else if (memcmp(S.Name, "UNIT", 4) == 0) ParseUNIT(S);
        else if (memcmp(S.Name, "THG2", 4) == 0 || memcmp(S.Name, "THG ", 4) == 0) ParseTHG2(S);
    }

    return true;
}

inline std::vector<ChkSectionInfo> ChkParser::CollectSections()
{
    std::vector<ChkSectionInfo> Sections;
    size_t Off = 0;
    while (Off + 8 <= mRawSize)
    {
        ChkSectionInfo S;
        memcpy(S.Name, mRawData + Off, 4);
        S.Size = (uint32_t)mRawData[Off + 4] |
                ((uint32_t)mRawData[Off + 5] << 8) |
                ((uint32_t)mRawData[Off + 6] << 16) |
                ((uint32_t)mRawData[Off + 7] << 24);
        S.Offset = Off + 8;

        if (Off + 8 + S.Size > mRawSize)
            break;

        Sections.push_back(S);
        Off += 8 + S.Size;
    }
    return Sections;
}

inline void ChkParser::ParseTYPE(const ChkSectionInfo& S)
{
    if (S.Size < 4) return;
    mMapTypeFlag = *(uint32_t*)(mRawData + S.Offset);
}

inline void ChkParser::ParseVER(const ChkSectionInfo& S)
{
    if (S.Size < 2) return;
    mFormatVersion = mRawData[S.Offset] | (mRawData[S.Offset + 1] << 8);
}

inline void ChkParser::ParseERA(const ChkSectionInfo& S)
{
    if (S.Size < 2) return;
    mTileset = (mRawData[S.Offset] | (mRawData[S.Offset + 1] << 8)) & 7;
}

inline void ChkParser::ParseDIM(const ChkSectionInfo& S)
{
    if (S.Size < 4) return;
    mMapWidth = mRawData[S.Offset] | (mRawData[S.Offset + 1] << 8);
    mMapHeight = mRawData[S.Offset + 2] | (mRawData[S.Offset + 3] << 8);
}

inline void ChkParser::ParseOWNR(const ChkSectionInfo& S)
{
    int N = S.Size < 12 ? (int)S.Size : 12;
    for (int I = 0; I < N; I++)
        mPlayers[I].Owner = mRawData[S.Offset + I];
}

inline void ChkParser::ParseSIDE(const ChkSectionInfo& S)
{
    int N = S.Size < 12 ? (int)S.Size : 12;
    for (int I = 0; I < N; I++)
        mPlayers[I].Race = mRawData[S.Offset + I];
}

inline void ChkParser::ParseFORC(const ChkSectionInfo& S)
{
    // First 8 bytes: player -> force assignment
    for (int I = 0; I < 8 && I < (int)S.Size; I++)
        mPlayers[I].Force = mRawData[S.Offset + I];

    if (S.Size < 8) return;

    // Bytes 8-15: force name string indices (4 x u16)
    int ForceNameOff = 8;
    for (int F = 0; F < 4 && ForceNameOff + 2 <= (int)S.Size; F++)
    {
        mForces[F].NameStringIndex =
            mRawData[S.Offset + ForceNameOff] |
            (mRawData[S.Offset + ForceNameOff + 1] << 8);
        ForceNameOff += 2;
    }

    // Bytes 16-19: force flags (4 x u8)
    int ForceFlagsOff = 16;
    for (int F = 0; F < 4 && ForceFlagsOff + F < (int)S.Size; F++)
        mForces[F].Flags = mRawData[S.Offset + ForceFlagsOff + F];
}

inline void ChkParser::ParseCOLR(const ChkSectionInfo& S)
{
    int N = S.Size < 8 ? (int)S.Size : 8;
    for (int I = 0; I < N; I++)
        mPlayers[I].Color = mRawData[S.Offset + I];
}

inline void ChkParser::ParseSPRP(const ChkSectionInfo& S)
{
    if (S.Size < 4) return;
    mNameStrIndex = mRawData[S.Offset] | (mRawData[S.Offset + 1] << 8);
    mDescStrIndex = mRawData[S.Offset + 2] | (mRawData[S.Offset + 3] << 8);
}

inline void ChkParser::ParseMTXM(const ChkSectionInfo& S)
{
    mMegaTiles.clear();
    size_t Count = S.Size / 2;
    mMegaTiles.reserve(Count);
    for (size_t I = 0; I < Count; I++)
    {
        uint16_t MT = mRawData[S.Offset + I * 2] | (mRawData[S.Offset + I * 2 + 1] << 8);
        mMegaTiles.push_back(MT);
    }
}

inline void ChkParser::ParseSTR(const ChkSectionInfo& S)
{
    mStringTable.Strings.clear();
    if (S.Size < 2) return;

    uint16_t Count = mRawData[S.Offset] | (mRawData[S.Offset + 1] << 8);
    if (S.Size < 2 + (size_t)Count * 2) return;

    mStringTable.Strings.reserve(Count);
    for (int I = 0; I < Count; I++)
    {
        uint16_t StrOff = mRawData[S.Offset + 2 + I * 2] |
                         (mRawData[S.Offset + 3 + I * 2] << 8);
        if (StrOff < S.Size)
        {
            const char* Str = reinterpret_cast<const char*>(mRawData + S.Offset + StrOff);
            mStringTable.Strings.push_back(Game::Locale::LegacyGameTextToUtf8(Str));
        }
        else
        {
            mStringTable.Strings.push_back({});
        }
    }
}

inline void ChkParser::ParseUNIT(const ChkSectionInfo& S)
{
    mUnits.clear();
    int Count = S.Size / 36;
    mUnits.reserve(Count);

    for (int I = 0; I < Count; I++)
    {
        // UNIT layout (36 bytes per CHK spec):
        //  0-3: u32 instance/serial number
        //  4-5: u16 X coordinate (pixels)
        //  6-7: u16 Y coordinate (pixels)
        //  8-9: u16 UnitID
        // 10-11: u16 Relation flags (bit9=nydus, bit10=addon)
        // 12-13: u16 Valid flags (bit0=cloak, bit1=burrow, ...)
        // 14-15: u16 Change flags (bit0=owner, bit1=hp, ...)
        // 16:   u8 Owner
        // 17:   u8 HP%
        // 18:   u8 Shield%
        // 19:   u8 Energy%
        // 20-23: u32 Resource amount
        // 24-25: u16 Hangar count
        // 26-27: u16 State flags
        // 28-31: u32 Unused
        // 32-35: u32 Link ID
        const uint8_t* U = mRawData + S.Offset + I * 36;
        PlacedUnit P;
        P.X           = U[4] | (U[5] << 8);
        P.Y           = U[6] | (U[7] << 8);
        P.UnitID      = U[8] | (U[9] << 8);
        P.RelationFlags = U[10] | (U[11] << 8);
        P.ValidFlags  = U[12] | (U[13] << 8);
        P.ChangeFlags = U[14] | (U[15] << 8);
        P.Owner       = U[16];
        P.HpPercent   = U[17];
        P.ShieldPercent = U[18];
        P.EnergyPercent = U[19];
        P.ResourceAmount = (uint32_t)U[20] | ((uint32_t)U[21] << 8) |
                          ((uint32_t)U[22] << 16) | ((uint32_t)U[23] << 24);
        P.HangarCount = U[24] | (U[25] << 8);
        P.StateFlags  = U[26] | (U[27] << 8);
        mUnits.push_back(P);

        // Extract start locations (unit ID 214 = StarCraft standard, 115 = legacy)
        if (P.UnitID == 214 || P.UnitID == 115)
        {
            StartLocation SL;
            SL.X = P.X;
            SL.Y = P.Y;
            SL.Owner = P.Owner;
            mStartLocations.push_back(SL);
        }
    }
}

inline void ChkParser::ParseTHG2(const ChkSectionInfo& S)
{
    size_t Count = S.Size / 10;
    for (size_t I = 0; I < Count; ++I)
    {
        size_t Offset = S.Offset + I * 10;
        if (Offset + 10 > mRawSize) break;
        const uint8_t* D = mRawData + Offset;
        PlacedDoodad P;
        P.ID = D[0] | (D[1] << 8);
        P.X = D[2] | (D[3] << 8);
        P.Y = D[4] | (D[5] << 8);
        P.Owner = D[6];
        P.Flags = D[9];
        P.IsSprite = (P.Flags & 0x10) != 0;
        mDoodads.push_back(P);
    }
}

} // namespace Game::Map
