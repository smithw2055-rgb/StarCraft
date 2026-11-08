#pragma once

#include <cstdint>
#include <vector>

namespace Resource
{

// ── Raw, raylib-agnostic color and image types ────────────
// Used by the decoders so the graphics target can link without raylib.
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

namespace Resource::Graphics
{
struct DecodedRgbaImage
{
    int Width = 0;
    int Height = 0;
    std::vector<uint8_t> Rgba;
};
} // namespace Resource::Graphics

// === Consolidated from src/Resource/DdsGrpDecoder.h ===


#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>



namespace Resource::Graphics
{

struct DdsGrpFrame
{
    int Width = 0;
    int Height = 0;
    uint32_t ByteSize = 0;
    size_t DataOffset = 0;
};

struct ParsedDdsGrp
{
    uint32_t DeclaredFileSize = 0;
    uint16_t FrameCount = 0;
    std::vector<DdsGrpFrame> Frames;
};

std::optional<ParsedDdsGrp> ParseDdsGrp(const std::vector<uint8_t>& Data);
std::optional<DecodedRgbaImage> LoadWicRgbaImageFromMemory(const uint8_t* Bytes, size_t ByteCount);
std::optional<std::vector<DecodedRgbaImage>> LoadDdsGrpRgbaFrames(const std::vector<uint8_t>& Data);

} // namespace Resource::Graphics
