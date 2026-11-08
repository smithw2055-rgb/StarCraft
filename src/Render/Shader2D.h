#pragma once

#include "Render/GpuTextureHandle.h"

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
    // x = shadow, y = Protoss warp texture, zw = texture crop scale.
    float EffectMode[4];
};

// Phase 4: 3-Channel HD PBR + DXT5nm Normal + TeamColor + Emissive Uniform Block
struct PBRSpriteShaderParams
{
    float TeamColor[4];   // rgb = player team color, a = team color intensity
    float LightDir[4];    // xyz = directional light vector (0.37, 0.09, 0.92), w = flip_x (1.0 or -1.0)
    float PBRConfig[4];   // x = has_normal_team_ao, y = has_spec_emissive, z = blend_mode (0=DeferredLit,1=Additive,2=Shadow), w = alpha_multiplier
};

// 2D pipeline. Vertex: position(float2) + uv(float2) + color(ubyte4 normalized).
// Uniform block: vs_params { mvp: mat4 }
// Texture sampler 0: image2D (set per draw call, or unbound for solid color)
struct Shader2D
{
    GpuShaderHandle Shader;
    GpuShaderHandle HealthBarShader;
    GpuShaderHandle SpriteEffectShader;
    GpuShaderHandle PBRSpriteShader;
    GpuPipelineHandle Pipeline;
    GpuPipelineHandle HealthBarPipeline;
    GpuPipelineHandle SpriteEffectPipeline;
    GpuPipelineHandle PipelineNoBlend;
    GpuPipelineHandle HealthBarPipelineNoBlend;
    GpuPipelineHandle SpriteEffectPipelineNoBlend;
    GpuPipelineHandle PBRSpritePipeline;
    GpuPipelineHandle PBRSpriteAdditivePipeline;
    GpuPipelineHandle PBRSpritePipelineNoBlend;
    GpuSamplerHandle Sampler;

    bool Init();
    void Shutdown();

    // Recreate the pipeline set for the requested blend mode.
    void RebuildPipeline(bool AlphaBlend, bool DepthEnabled);

    // Bind the pipeline + default sampler
    void BindPipeline(bool AlphaBlend = true);
    void BindHealthBarPipeline(bool AlphaBlend = true);
    void BindSpriteEffectPipeline(bool AlphaBlend = true);
    void ApplyHealthBarParams(const HealthBarShaderParams& Params);
    void ApplySpriteEffectParams(const SpriteEffectShaderParams& Params);
    void BindPBRSpritePipeline(bool AlphaBlend = true, bool AdditiveBlend = false);
    void ApplyPBRSpriteParams(const PBRSpriteShaderParams& Params);
    void BindDefaultSampler();
    const GpuSamplerHandle& GetSampler() const { return Sampler; }

private:
    bool InitTexturedShader();
    bool InitHealthBarShader();
    bool InitSpriteEffectShader();
    bool InitPBRSpriteShader();
    void RebuildPipelineFor(GpuShaderHandle ShaderHandle,
                            GpuPipelineHandle& PipelineHandle,
                            bool AlphaBlend,
                            bool DepthEnabled);
};

} // namespace Render
