#pragma once

#include "third_party/sokol/sokol_gfx.h"

namespace Render
{

// 2D pipeline. Vertex: position(float2) + uv(float2) + color(ubyte4 normalized).
// Uniform block: vs_params { mvp: mat4 }
// Texture sampler 0: image2D (set per draw call, or unbound for solid color)
struct Shader2D
{
    sg_shader Shader = {0};
    sg_pipeline Pipeline = {0};
    sg_buffer Vbuf = {0};
    sg_buffer Ibuf = {0};
    sg_sampler Sampler = {0};

    bool Init();
    void Shutdown();

    // Recreate pipeline (call when blending / depth changes)
    void RebuildPipeline(bool AlphaBlend, bool DepthEnabled);

    // Bind the pipeline + default sampler
    void BindPipeline();
    void BindDefaultSampler();
};

} // namespace Render
