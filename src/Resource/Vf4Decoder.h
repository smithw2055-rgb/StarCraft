#pragma once

#include <vector>
#include <cstdint>
#include <array>

namespace Resource
{

struct Vf4Entry
{
    std::array<uint16_t, 16> Flags = {};

    bool IsMinitleWalkable(int Index) const
    {
        return Index >= 0 && Index < 16 && (Flags[Index] & 1) != 0;
    }

    bool IsMinitleHigh(int Index) const
    {
        return Index >= 0 && Index < 16 && (Flags[Index] & 4) != 0;
    }

    bool IsMinitleVeryHigh(int Index) const
    {
        return Index >= 0 && Index < 16 && (Flags[Index] & 8) != 0;
    }

    bool IsMinitleMiddle(int Index) const
    {
        return Index >= 0 && Index < 16 && (Flags[Index] & 2) != 0;
    }
};

struct Vf4Data
{
    std::vector<Vf4Entry> Entries;
};

class Vf4Decoder
{
public:
    static Vf4Data Decode(const std::vector<uint8_t>& Data);
    static constexpr int EntryBytes = 32; // 16 × uint16_t
};

} // namespace Resource
