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
