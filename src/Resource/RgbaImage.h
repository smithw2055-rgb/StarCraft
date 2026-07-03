#pragma once

#include <cstdint>
#include <vector>

namespace Resource::Graphics
{

struct DecodedRgbaImage
{
    int Width = 0;
    int Height = 0;
    std::vector<uint8_t> Rgba;
};

} // namespace Resource::Graphics
