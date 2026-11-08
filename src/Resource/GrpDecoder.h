#pragma once

#include <string>
#include <vector>
#include <optional>
#include <memory>

#include "Resource/RawColor.h"

namespace Resource
{

struct GrpFrameInfo
{
    int Width = 0;
    int Height = 0;
    int XOffset = 0;
    int YOffset = 0;
    int Offset = 0;
    bool Flip = false;
};

struct GrpSprite
{
    int FrameCount = 0;
    int Width = 0;
    int Height = 0;
    std::vector<GrpFrameInfo> Frames;
    std::vector<uint8_t> RawFrameData;  // concatenated uncompressed frame pixels
};

class GrpDecoder
{
public:
    // Decode just the sprite metadata + raw indexed pixel data.
    static std::optional<GrpSprite> DecodeGrp(const std::vector<uint8_t>& GrpData);

    // Decode to a list of RGBA8 raw images, one per frame, with palette applied.
    static std::optional<std::vector<RawImage>> DecodeToRawImages(
        const std::vector<uint8_t>& GrpData,
        const RawPalette& Pal);

private:
    static std::vector<uint8_t> DecodeFrameRle(const uint8_t* CompressedData,
                                                int CompressedSize,
                                                int Width, int Height);
};

} // namespace Resource

// === Consolidated from src/Resource/GrpToGpu.h ===
#include <cstdint>
#include <array>
#include <optional>
#include <string>
#include <vector>

#include "Resource/UIAssetTypes.h"
#include "Resource/RawColor.h"
#include "Resource/Palette.h"

namespace Resource::Graphics
{

struct DecodedFrame
{
    std::vector<uint8_t> Rgba; // RGBA8888
    std::vector<uint8_t> TeamColorMaskRgba; // RGBA8888, red channel is remap weight
    int Width = 0;
    int Height = 0;
    int OriginX = 0;
    int OriginY = 0;
    bool Flip = false;
};

struct DecodedSprite
{
    std::vector<DecodedFrame> Frames;
    int FrameCount() const { return static_cast<int>(Frames.size()); }
};

struct GrpDecodeOptions
{
    const ColorPalette* PaletteOverride = nullptr;
    bool TransparentIndexZero = true;
    bool TransparentBlack = false;
    bool UseIndexRemap = false;
    bool GenerateTeamColorMask = true;
    // Modifier 9 GRPs store light-table row indices rather than palette
    // colors. Decode those indices as a graded RGBA glow mask so the modern
    // renderer does not display the whole source frame as a white disc.
    bool DecodeLightTableMask = false;
    bool UseLightTableRgba = false;
    std::array<RawColor, 256> LightTableRgba = {};
    uint8_t LightTableIndexCount = 63;
    uint8_t LightTableMaxAlpha = 192;
    RawColor LightTableTint = RawColor(255, 160, 64, 255);
    uint8_t TeamColorIndexStart = 8;
    uint8_t TeamColorIndexCount = 8;
    int16_t IndexRemap[256] = {};
    RawColor TransparentRgb = RawColor(0, 0, 0, 0);

    GrpDecodeOptions()
    {
        for (int I = 0; I < 256; ++I)
            IndexRemap[I] = -1;
    }
};

constexpr uint8_t DecodeLightTableMaskAlpha(uint8_t Index,
                                             uint8_t IndexCount,
                                             uint8_t MaxAlpha)
{
    if (Index == 0 || IndexCount == 0 || Index > IndexCount)
        return 0;
    if (IndexCount == 1)
        return MaxAlpha;
    return static_cast<uint8_t>(
        (static_cast<unsigned>(Index - 1) * MaxAlpha) /
        static_cast<unsigned>(IndexCount - 1));
}

std::optional<DecodedSprite> DecodeGrpToRgba(
    const std::vector<uint8_t>& GrpData,
    const ColorPalette& Pal,
    const GrpDecodeOptions& Options = {});


GpuImage UploadRgbaFull(int Width, int Height, const uint8_t* Rgba);

void DestroyImage(GpuImage& Img);

} // namespace Resource::Graphics

