#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <optional>

namespace Resource::SokolBridge
{

// Independent color type to avoid raylib pollution on the sokol side.
struct SokoColor
{
    uint8_t R = 0;
    uint8_t G = 0;
    uint8_t B = 0;
    uint8_t A = 255;
};

struct SokoPalette
{
    SokoColor Colors[256] = {};
};

// Decode a .pal (768 bytes RGB or 1024 bytes RGBX)
std::optional<SokoPalette> DecodePalSokol(const std::vector<uint8_t>& Data);

// Decode a .wpe (1024 bytes RGBA, no header)
std::optional<SokoPalette> DecodeWpeSokol(const std::vector<uint8_t>& Data);

// Synthesised default palette (rainbow spread, alpha 0 on idx 0)
SokoPalette GetDefaultSokolPalette();

} // namespace Resource::SokolBridge
