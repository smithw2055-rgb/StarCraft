#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "third_party/sokol/sokol_gfx.h"

#include "Resource/GrpDecoder.h"
#include "Resource/RawColor.h"
#include "Resource/sokol/PaletteSokol.h"

namespace Resource::SokolBridge
{

struct DecodedFrame
{
    std::vector<uint8_t> Rgba; // RGBA8888
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
    const SokoPalette* PaletteOverride = nullptr;
    bool TransparentIndexZero = true;
    bool TransparentBlack = false;
    bool UseIndexRemap = false;
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
    const SokoPalette& Pal,
    const GrpDecodeOptions& Options = {});

sg_image UploadRgba(int Width, int Height, const uint8_t* Rgba);

struct SokolImage
{
    sg_image Image = {0};
    sg_view View = {0};
    int Width = 0;
    int Height = 0;
    bool Valid() const { return Image.id != 0; }
};

SokolImage UploadRgbaFull(int Width, int Height, const uint8_t* Rgba);

void DestroyImage(SokolImage& Img);

} // namespace Resource::SokolBridge
