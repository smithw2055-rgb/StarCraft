#pragma once

#include "Render/GpuShaderHandle.h"

namespace Render
{

// 2D pipeline. Vertex: position(float2) + uv(float2) + color(ubyte4 normalized).
// Uniform block: vs_params { mvp: mat4 }
// Texture sampler 0: image2D (set per draw call, or unbound for solid color)
struct Shader2D
{
    GpuShaderHandle Shader;
    GpuPipelineHandle Pipeline;
    GpuSamplerHandle Sampler;

    bool Init();
    void Shutdown();

    // Recreate pipeline (call when blending / depth changes)
    void RebuildPipeline(bool AlphaBlend, bool DepthEnabled);

    // Bind the pipeline + default sampler
    void BindPipeline();
    void BindDefaultSampler();
    const GpuSamplerHandle& GetSampler() const { return Sampler; }
};

} // namespace Render
