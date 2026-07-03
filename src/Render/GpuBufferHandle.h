#pragma once

#include <cstdint>

namespace Render
{

struct GpuBufferObjectHandle
{
    uint32_t id = 0;
};

struct GpuBufferHandle
{
    GpuBufferObjectHandle Buffer = {};

    bool Valid() const { return Buffer.id != 0; }
};

} // namespace Render
