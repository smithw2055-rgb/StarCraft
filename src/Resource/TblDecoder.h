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
