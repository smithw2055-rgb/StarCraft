#pragma once

#include "Render/GpuShaderHandle.h"

namespace Render
{

struct HealthBarShaderParams
{
    float SolidColor[4];
    float MultiplyColor[4];
    float TeamColor[4];
};

struct SpriteEffectShaderParams
{
    float MultiplyColor[4];
    float TeamColor[4];
    // x = hallucination, y = cloaked, z = brighten, w = use team color.
    float EffectData[4];
    // x = shadow mask rendered directly in the forward 2D path.
    float EffectMode[4];
};

// 2D pipeline. Vertex: position(float2) + uv(float2) + color(ubyte4 normalized).
// Uniform block: vs_params { mvp: mat4 }
// Texture sampler 0: image2D (set per draw call, or unbound for solid color)
struct Shader2D
{
    GpuShaderHandle Shader;
    GpuShaderHandle HealthBarShader;
    GpuShaderHandle SpriteEffectShader;
    GpuPipelineHandle Pipeline;
    GpuPipelineHandle HealthBarPipeline;
    GpuPipelineHandle SpriteEffectPipeline;
    GpuSamplerHandle Sampler;

    bool Init();
    void Shutdown();

    // Recreate pipeline (call when blending / depth changes)
    void RebuildPipeline(bool AlphaBlend, bool DepthEnabled);

    // Bind the pipeline + default sampler
    void BindPipeline();
    void BindHealthBarPipeline();
    void BindSpriteEffectPipeline();
    void ApplyHealthBarParams(const HealthBarShaderParams& Params);
    void ApplySpriteEffectParams(const SpriteEffectShaderParams& Params);
    void BindDefaultSampler();
    const GpuSamplerHandle& GetSampler() const { return Sampler; }

private:
    bool InitTexturedShader();
    bool InitHealthBarShader();
    bool InitSpriteEffectShader();
    void RebuildPipelineFor(GpuShaderHandle ShaderHandle,
                            GpuPipelineHandle& PipelineHandle,
                            bool AlphaBlend,
                            bool DepthEnabled);
};

} // namespace Render
