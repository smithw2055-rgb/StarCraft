#pragma once

#include "Render/GpuTextureHandle.h"

namespace Resource::Graphics
{

struct GpuImage : Render::GpuTextureHandle
{
    int Width = 0;
    int Height = 0;
    Render::GpuTextureHandle TeamColorMask = {};
    bool ShadowSprite = false;
    bool HasVisiblePixels = false;
};

} // namespace Resource::Graphics
