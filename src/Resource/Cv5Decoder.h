#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Core/Utils/Logger.h"

namespace Resource
{

struct Cv5Entry
{
    uint16_t Flags = 0;
    uint16_t MegaTileIndices[16] = {};

    bool IsWalkable() const { return (Flags & 1) != 0; }
    bool IsBuildable() const { return !(Flags & 0x80); }
    bool HasCreep() const { return (Flags & 0x40) != 0; }
    bool IsPartiallyWalkable() const { return (Flags & 0x2000) != 0; }
};

class Cv5Decoder
{
public:
    static std::vector<Cv5Entry> Decode(const std::vector<uint8_t>& Data);

    static constexpr int EntrySize = 52;
    static constexpr int HeaderBytes = 20;
    static constexpr int SubTiles = 16;
};

struct MegaTileEntry
{
    uint32_t Index = 0;
    uint16_t MiniTileIDs[16] = {};
};

struct Vx4Data
{
    std::vector<MegaTileEntry> MegaTiles;
    size_t MegaTileCount = 0;
};

class Vx4Decoder
{
public:
    static Vx4Data Decode(const std::vector<uint8_t>& Data);

    static constexpr int MiniTilesPerMegaTile = 16;
};

namespace Detail
{
inline uint16_t ReadU16(const uint8_t* Data, size_t Offset)
{
    return Data[Offset] | (Data[Offset + 1] << 8);
}
} // namespace Detail

inline std::vector<Cv5Entry> Cv5Decoder::Decode(const std::vector<uint8_t>& Data)
{
    std::vector<Cv5Entry> Entries;

    if (Data.size() < EntrySize)
    {
        Core::Utils::Logger::Warning("Cv5Decoder", "CV5 data too small");
        return Entries;
    }

    size_t Count = Data.size() / EntrySize;
    Entries.reserve(Count);

    for (size_t I = 0; I < Count; ++I)
    {
        size_t Off = I * EntrySize;
        if (Off + EntrySize > Data.size())
            break;

        Cv5Entry E;
        E.Flags = Detail::ReadU16(Data.data(), Off + 0);

        // MegaTile indices: offset 20-51 (16 × uint16, after 20 bytes header)
        for (int J = 0; J < SubTiles; J++)
        {
            E.MegaTileIndices[J] = Detail::ReadU16(Data.data(), Off + 20 + J * 2);
        }

        Entries.push_back(E);
    }

    Core::Utils::Logger::Info("Cv5Decoder",
        "Decoded " + std::to_string(Entries.size()) + " CV5 entries");
    return Entries;
}

inline Vx4Data Vx4Decoder::Decode(const std::vector<uint8_t>& Data)
{
    Vx4Data Result;

    if (Data.empty() || Data.size() % 2 != 0)
    {
        Core::Utils::Logger::Warning("Vx4Decoder", "VX4 data size invalid");
        return Result;
    }

    size_t TotalValues = Data.size() / 2;
    Result.MegaTileCount = TotalValues / MiniTilesPerMegaTile;
    Result.MegaTiles.reserve(Result.MegaTileCount);

    for (size_t M = 0; M < Result.MegaTileCount; M++)
    {
        MegaTileEntry Entry;
        Entry.Index = (uint32_t)M;

        for (int J = 0; J < MiniTilesPerMegaTile; J++)
        {
            size_t Off = (M * MiniTilesPerMegaTile + J) * 2;
            Entry.MiniTileIDs[J] = Detail::ReadU16(Data.data(), Off);
        }

        Result.MegaTiles.push_back(Entry);
    }

    Core::Utils::Logger::Info("Vx4Decoder",
        "Decoded " + std::to_string(Result.MegaTileCount) + " megatile entries");
    return Result;
}

} // namespace Resource
