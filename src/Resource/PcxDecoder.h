#pragma once

#include <vector>
#include <cstdint>

#include "Resource/RawColor.h"
#include "Resource/PcxRegion.h"

namespace Resource
{

struct PcxResult
{
    RawImage Texture;
    PcxRegions Regions;
};

class PcxDecoder
{
public:
    static PcxResult Decode(const std::vector<uint8_t>& Data);
};

} // namespace Resource
