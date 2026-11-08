#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "Resource/RawColor.h"

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
