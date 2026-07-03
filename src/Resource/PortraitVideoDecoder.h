#pragma once

#include <filesystem>
#include <optional>
#include <vector>

#include "Resource/RgbaImage.h"

namespace Resource::Graphics
{

struct DecodedVideoFrame
{
    DecodedRgbaImage Image;
    float DurationSeconds = 1.0f / 12.0f;
};

std::optional<std::vector<DecodedVideoFrame>> DecodeVideoFramesFromFile(const std::filesystem::path& Path);
std::optional<DecodedVideoFrame> DecodeFirstVideoFrameWithFfmpeg(const std::filesystem::path& Path);

} // namespace Resource::Graphics
