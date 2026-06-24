#pragma once

#include <cstdint>
#include <vector>

namespace Resource
{

// ── Raw, raylib-agnostic color and image types ────────────
// Used by the decoders so the sokol target can link without raylib.
// The raylib-based main game converts to raylib::Color / raylib::Image
// via the adapters in Resource/raylib/.

struct RawColor
{
    uint8_t R = 0;
    uint8_t G = 0;
    uint8_t B = 0;
    uint8_t A = 255;

    constexpr RawColor() = default;
    constexpr RawColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
        : R(r), G(g), B(b), A(a) {}
};

struct RawPalette
{
    RawColor Colors[256] = {};
};

struct RawImage
{
    int Width = 0;
    int Height = 0;
    std::vector<uint8_t> Rgba;   // width*height*4 bytes (RGBA8)

    bool Valid() const { return Width > 0 && Height > 0 && (int)Rgba.size() == Width * Height * 4; }
    int PixelCount() const { return Width * Height; }
};

} // namespace Resource
