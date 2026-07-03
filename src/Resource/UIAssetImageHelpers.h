#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "Resource/Palette.h"
#include "Resource/RgbaImage.h"
#include "Resource/UIAssetTypes.h"

namespace Resource::Graphics
{

ImageOpaqueBounds ComputeOpaqueBounds(const uint8_t* Rgba, int Width, int Height);
std::optional<ColorPalette> LoadPcxPalette(const std::vector<uint8_t>& Data);
void ApplyTransparentBlack(std::vector<uint8_t>& Rgba);
void ConvertOpaquePixelsToWhiteMask(std::vector<uint8_t>& Rgba);
std::optional<DecodedRgbaImage> ResizeRgbaImage(const DecodedRgbaImage& Source,
                                                int TargetWidth,
                                                int TargetHeight);
void AlphaBlendInto(std::vector<uint8_t>& Dest,
                    int DestWidth,
                    int DestHeight,
                    const DecodedRgbaImage& Source,
                    int DestX,
                    int DestY);

} // namespace Resource::Graphics
