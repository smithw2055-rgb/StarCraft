#include "Resource/Cv5Decoder.h"
#include "Core/Utils/Logger.h"

namespace Resource
{

static inline uint16_t ReadU16(const uint8_t* Data, size_t Offset)
{
    return Data[Offset] | (Data[Offset + 1] << 8);
}

std::vector<Cv5Entry> Cv5Decoder::Decode(const std::vector<uint8_t>& Data)
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
        E.Flags = ReadU16(Data.data(), Off + 0);

        // MegaTile indices: offset 20-51 (16 × uint16, after 20 bytes header)
        for (int J = 0; J < SubTiles; J++)
        {
            E.MegaTileIndices[J] = ReadU16(Data.data(), Off + 20 + J * 2);
        }

        Entries.push_back(E);
    }

    Core::Utils::Logger::Info("Cv5Decoder",
        "Decoded " + std::to_string(Entries.size()) + " CV5 entries");
    return Entries;
}

Vx4Data Vx4Decoder::Decode(const std::vector<uint8_t>& Data)
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
            Entry.MiniTileIDs[J] = ReadU16(Data.data(), Off);
        }

        Result.MegaTiles.push_back(Entry);
    }

    Core::Utils::Logger::Info("Vx4Decoder",
        "Decoded " + std::to_string(Result.MegaTileCount) + " megatile entries");
    return Result;
}

} // namespace Resource
