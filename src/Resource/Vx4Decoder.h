#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace Resource
{

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

} // namespace Resource
