#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "Resource/RawColor.h"

namespace Resource
{

class PalDecoder
{
public:
    static std::optional<RawPalette> DecodePal(const std::vector<uint8_t>& Data);
    static std::optional<RawPalette> DecodeWpe(const std::vector<uint8_t>& Data);

    static RawImage ApplyPalette(const std::vector<uint8_t>& PixelData,
                                 int Width, int Height,
                                 const RawPalette& Pal,
                                 RawColor TransparentColor = RawColor(0, 0, 0, 0));

    static RawPalette GetDefaultPalette();
};

} // namespace Resource

namespace Resource::Graphics
{

struct SokoColor
{
    uint8_t R = 0;
    uint8_t G = 0;
    uint8_t B = 0;
    uint8_t A = 255;
};

struct ColorPalette
{
    SokoColor Colors[256] = {};
};

std::optional<ColorPalette> DecodePal(const std::vector<uint8_t>& Data);
std::optional<ColorPalette> DecodeWpe(const std::vector<uint8_t>& Data);
ColorPalette GetDefaultPalette();

} // namespace Resource::Graphics
