#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <optional>

#include "Resource/PcxRegion.h"

#include "Resource/sokol/GrpToSokol.h"

namespace Resource::SokolBridge
{

struct DecodedPcx
{
    int Width = 0;
    int Height = 0;
    std::vector<uint8_t> Rgba;        // RGBA8888 with red-mask → transparent
    PcxRegions Regions;
};

std::optional<DecodedPcx> DecodePcxToRgba(const std::vector<uint8_t>& Data);

} // namespace Resource::SokolBridge
