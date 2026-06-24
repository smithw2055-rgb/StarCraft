#pragma once

#include <optional>
#include <string>
#include <vector>

#include "Resource/RawColor.h"

namespace Resource
{

// ── Palette decoders (raylib-agnostic) ────────────────────
// Returns the 256-color palette that PCX / WPE files encode.

class PalDecoder
{
public:
    static std::optional<RawPalette> DecodePal(const std::vector<uint8_t>& Data);
    static std::optional<RawPalette> DecodeWpe(const std::vector<uint8_t>& Data);

    // Apply a 256-color palette to indexed pixel data, producing an RGBA8 image.
    // TransparentColor: palette entries equal to this color become alpha=0.
    static RawImage ApplyPalette(const std::vector<uint8_t>& PixelData,
                                 int Width, int Height,
                                 const RawPalette& Pal,
                                 RawColor TransparentColor = RawColor(0, 0, 0, 0));

    static RawPalette GetDefaultPalette();
};

} // namespace Resource
