#pragma once

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
