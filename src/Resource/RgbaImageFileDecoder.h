#pragma once

#include <filesystem>
#include <optional>

#include "Resource/RgbaImage.h"

namespace Resource::Graphics
{

std::optional<DecodedRgbaImage> DecodeRgbaImageFile(const std::filesystem::path& Path);
std::optional<DecodedRgbaImage> LoadWicRgbaImageFromFile(const std::filesystem::path& Path);

} // namespace Resource::Graphics

