#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace Resource
{

struct TblEntry
{
    uint32_t Index = 0;
    std::string Text;
};

class TblDecoder
{
public:
    static std::vector<TblEntry> Decode(const std::vector<uint8_t>& Data);

    static std::vector<std::string> DecodeStrings(const std::vector<uint8_t>& Data);
};

} // namespace Resource

// === Inlined from src/Resource/TblDecoder.cpp ===

#include "Core/Utils/Logger.h"

namespace Resource
{

inline uint16_t ReadTblU16(const uint8_t* Data, size_t Offset)
{
    return Data[Offset] | (Data[Offset + 1] << 8);
}

inline uint32_t ReadTblU32(const uint8_t* Data, size_t Offset)
{
    return Data[Offset] | (Data[Offset + 1] << 8) |
           (Data[Offset + 2] << 16) | (Data[Offset + 3] << 24);
}

inline std::vector<TblEntry> TblDecoder::Decode(const std::vector<uint8_t>& Data)
{
    std::vector<TblEntry> Entries;
    if (Data.size() < 2)
    {
        Core::Utils::Logger::Warning("TblDecoder", "TBL data too small");
        return Entries;
    }

    uint16_t Count = ReadTblU16(Data.data(), 0);
    size_t OffsetTableEnd = 2 + Count * 2;
    if (OffsetTableEnd > Data.size())
    {
        Core::Utils::Logger::Warning("TblDecoder",
            "TBL offset table exceeds data size");
        return Entries;
    }

    Entries.reserve(Count);
    for (uint16_t I = 0; I < Count; I++)
    {
        TblEntry E;
        E.Index = I;
        uint16_t StrOff = ReadTblU16(Data.data(), 2 + I * 2);
        if (StrOff < Data.size())
        {
            size_t MaxLen = Data.size() - StrOff;
            const char* Str = reinterpret_cast<const char*>(Data.data() + StrOff);
            size_t Len = 0;
            while (Len < MaxLen && Str[Len] != '\0')
                Len++;
            E.Text.assign(Str, Len);
        }
        Entries.push_back(E);
    }

    Core::Utils::Logger::Info("TblDecoder",
        "Decoded " + std::to_string(Entries.size()) + " TBL entries");
    return Entries;
}

inline std::vector<std::string> TblDecoder::DecodeStrings(const std::vector<uint8_t>& Data)
{
    auto Entries = Decode(Data);
    std::vector<std::string> Strings;
    Strings.reserve(Entries.size());
    for (auto& E : Entries)
        Strings.push_back(std::move(E.Text));
    return Strings;
}

} // namespace Resource
