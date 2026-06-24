#pragma once

#include <string>
#include <vector>
#include <cstdint>

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

} // namespace Resource
