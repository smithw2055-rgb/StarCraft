#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "Resource/GpuImage.h"

#include "Resource/GrpDecoder.h"
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

std::optional<DecodedSprite> DecodeGrpToRgba(
    const std::vector<uint8_t>& GrpData,
    const ColorPalette& Pal,
    const GrpDecodeOptions& Options = {});


GpuImage UploadRgbaFull(int Width, int Height, const uint8_t* Rgba);

void DestroyImage(GpuImage& Img);

} // namespace Resource::Graphics
