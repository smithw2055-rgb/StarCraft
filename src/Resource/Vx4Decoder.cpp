#include "Resource/Vx4Decoder.h"
#include "Core/Utils/Logger.h"

namespace Resource
{

static inline uint16_t ReadU16(const uint8_t* Data, size_t Offset)
{
    return Data[Offset] | (Data[Offset + 1] << 8);
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
