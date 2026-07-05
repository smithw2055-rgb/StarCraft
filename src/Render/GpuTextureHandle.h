#pragma once

#include <cstdint>

namespace Render
{

struct GpuImageHandle
{
    uint32_t id = 0;
};

struct GpuViewHandle
{
    uint32_t id = 0;
};

struct GpuTextureHandle
{
    GpuImageHandle Image = {};
    GpuViewHandle View = {};

    bool Valid() const { return Image.id != 0 && View.id != 0; }
};

} // namespace Render
