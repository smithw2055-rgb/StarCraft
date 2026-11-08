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

// === Consolidated from GpuBufferHandle.h & GpuShaderHandle.h ===
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

#include <cstdint>

namespace Render
{

struct GpuShaderObjectHandle
{
    uint32_t id = 0;
};

struct GpuPipelineObjectHandle
{
    uint32_t id = 0;
};

struct GpuSamplerObjectHandle
{
    uint32_t id = 0;
};

struct GpuShaderHandle
{
    GpuShaderObjectHandle Shader = {};
    bool Valid() const { return Shader.id != 0; }
};

struct GpuPipelineHandle
{
    GpuPipelineObjectHandle Pipeline = {};
    bool Valid() const { return Pipeline.id != 0; }
};

struct GpuSamplerHandle
{
    GpuSamplerObjectHandle Sampler = {};
    bool Valid() const { return Sampler.id != 0; }
};

} // namespace Render
