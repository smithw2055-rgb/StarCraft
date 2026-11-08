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

// === Inlined from src/Resource/Vf4Decoder.cpp ===

#include "Core/Utils/Logger.h"

namespace Resource
{

inline Vf4Data Vf4Decoder::Decode(const std::vector<uint8_t>& Data)
{
    Vf4Data Result;

    if (Data.empty() || Data.size() % EntryBytes != 0)
    {
        Core::Utils::Logger::Warning("Vf4Decoder", "VF4 data size invalid");
        return Result;
    }

    size_t Count = Data.size() / EntryBytes;
    Result.Entries.reserve(Count);

    for (size_t I = 0; I < Count; I++)
    {
        size_t Off = I * EntryBytes;
        Vf4Entry Entry;
        for (int J = 0; J < 16; J++)
        {
            size_t ByteOff = Off + J * 2;
            if (ByteOff + 1 >= Data.size()) break;
            Entry.Flags[J] = Data[ByteOff] | (Data[ByteOff + 1] << 8);
        }
        Result.Entries.push_back(Entry);
    }

    Core::Utils::Logger::Info("Vf4Decoder",
        "Decoded " + std::to_string(Count) + " VF4 entries");
    return Result;
}

} // namespace Resource
