#pragma once

#include <vector>
#include <cstdint>

namespace Resource
{

struct PcxRegion
{
    int X = 0, Y = 0, W = 0, H = 0;
};

struct PcxRegions
{
    std::vector<PcxRegion> Minimap;
    std::vector<PcxRegion> Portrait;
    std::vector<PcxRegion> Info;
    std::vector<PcxRegion> CmdButtons;
    std::vector<PcxRegion> Resources;
};

} // namespace Resource
