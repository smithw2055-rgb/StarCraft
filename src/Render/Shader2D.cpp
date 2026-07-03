#include "Render/Shader2D.h"
#include "Render/Private/SokolGpuInterop.h"

#include "Core/Utils/Logger.h"

namespace Render
{

static const char* kVSGLSL = R"(
#version 330 core
layout(location = 0) in vec2 a_position;
layout(location = 1) in vec2 a_texcoord0;
layout(location = 2) in vec4 a_color0;
uniform mat4 u_mvp;
out vec2 v_uv;
out vec4 v_color;
void main() {
    v_uv = a_texcoord0;
    v_color = a_color0;
    gl_Position = u_mvp * vec4(a_position, 0.0, 1.0);
}
)";

static const char* kFSGLSL = R"(
#version 330 core
in vec2 v_uv;
in vec4 v_color;
out vec4 frag_color;
uniform sampler2D u_tex;
void main() {
    frag_color = texture(u_tex, v_uv) * v_color;
}
)";

static const char* kVSHLSL = R"(
cbuffer Params : register(b0)
{
    float4x4 u_mvp;
};

struct VSInput
{
    float2 position : POSITION;
    float2 texcoord0 : TEXCOORD0;
    float4 color0 : COLOR0;
};

struct VSOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    output.uv = input.texcoord0;
    output.color = input.color0;
    output.position = mul(u_mvp, float4(input.position, 0.0f, 1.0f));
    return output;
}
)";

static const char* kFSHLSL = R"(
Texture2D u_tex : register(t0);
SamplerState u_smp : register(s0);

struct PSInput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

float4 main(PSInput input) : SV_Target
{
    return u_tex.Sample(u_smp, input.uv) * input.color;
}
)";

bool Shader2D::Init()
{
    sg_shader_desc Desc = {};
#if defined(SOKOL_D3D11)
    Desc.vertex_func.source = kVSHLSL;
    Desc.vertex_func.entry = "main";
    Desc.vertex_func.d3d11_target = "vs_4_0";
    Desc.fragment_func.source = kFSHLSL;
    Desc.fragment_func.entry = "main";
    Desc.fragment_func.d3d11_target = "ps_4_0";
#else
    Desc.vertex_func.source = kVSGLSL;
    Desc.vertex_func.entry = "main";
    Desc.fragment_func.source = kFSGLSL;
    Desc.fragment_func.entry = "main";
#endif

    Desc.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
    Desc.uniform_blocks[0].size = 64;
#if defined(SOKOL_D3D11)
    Desc.uniform_blocks[0].hlsl_register_b_n = 0;
#else
    Desc.uniform_blocks[0].layout = SG_UNIFORMLAYOUT_STD140;
    Desc.uniform_blocks[0].msl_buffer_n = 0;
    Desc.uniform_blocks[0].wgsl_group0_binding_n = 0;
    Desc.uniform_blocks[0].glsl_uniforms[0].type = SG_UNIFORMTYPE_MAT4;
    Desc.uniform_blocks[0].glsl_uniforms[0].array_count = 1;
    Desc.uniform_blocks[0].glsl_uniforms[0].glsl_name = "u_mvp";
#endif

    Desc.views[0].texture.stage = SG_SHADERSTAGE_FRAGMENT;
    Desc.views[0].texture.image_type = SG_IMAGETYPE_2D;
    Desc.views[0].texture.sample_type = SG_IMAGESAMPLETYPE_FLOAT;
    Desc.views[0].texture.multisampled = false;
#if defined(SOKOL_D3D11)
    Desc.views[0].texture.hlsl_register_t_n = 0;
#endif

    Desc.samplers[0].stage = SG_SHADERSTAGE_FRAGMENT;
    Desc.samplers[0].sampler_type = SG_SAMPLERTYPE_FILTERING;
#if defined(SOKOL_D3D11)
    Desc.samplers[0].hlsl_register_s_n = 0;
#endif

    Desc.texture_sampler_pairs[0].stage = SG_SHADERSTAGE_FRAGMENT;
    Desc.texture_sampler_pairs[0].view_slot = 0;
    Desc.texture_sampler_pairs[0].sampler_slot = 0;
#if !defined(SOKOL_D3D11)
    Desc.texture_sampler_pairs[0].glsl_name = "u_tex";
#endif

    Desc.attrs[0].base_type = SG_SHADERATTRBASETYPE_FLOAT;
#if defined(SOKOL_D3D11)
    Desc.attrs[0].hlsl_sem_name = "POSITION";
    Desc.attrs[0].hlsl_sem_index = 0;
#else
    Desc.attrs[0].glsl_name = "a_position";
#endif
    Desc.attrs[1].base_type = SG_SHADERATTRBASETYPE_FLOAT;
#if defined(SOKOL_D3D11)
    Desc.attrs[1].hlsl_sem_name = "TEXCOORD";
    Desc.attrs[1].hlsl_sem_index = 0;
#else
    Desc.attrs[1].glsl_name = "a_texcoord0";
#endif
    Desc.attrs[2].base_type = SG_SHADERATTRBASETYPE_FLOAT;
#if defined(SOKOL_D3D11)
    Desc.attrs[2].hlsl_sem_name = "COLOR";
    Desc.attrs[2].hlsl_sem_index = 0;
#else
    Desc.attrs[2].glsl_name = "a_color0";
#endif

    Shader.Shader = Render::FromSokol(sg_make_shader(&Desc));
    if (!Shader.Valid())
    {
        Core::Utils::Logger::Error("Shader2D", "sg_make_shader failed");
        return false;
    }

    sg_sampler_desc SDesc = {};
    SDesc.min_filter = SG_FILTER_LINEAR;
    SDesc.mag_filter = SG_FILTER_LINEAR;
    SDesc.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
    SDesc.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
    Sampler.Sampler = Render::FromSokol(sg_make_sampler(&SDesc));
    if (!Sampler.Valid())
    {
        Core::Utils::Logger::Error("Shader2D", "sg_make_sampler failed");
    }

    // Create a dummy 1x1 white image (used when drawing pure color quads)
    uint8_t White[4] = { 255, 255, 255, 255 };
    sg_image_desc IDesc = {};
    IDesc.width = 1;
    IDesc.height = 1;
    IDesc.pixel_format = SG_PIXELFORMAT_RGBA8;
    IDesc.data.mip_levels[0] = { White, 4 };
    IDesc.label = "white1x1";
    // Stored as static so it lives forever
    static sg_image sWhiteImage = sg_make_image(&IDesc);
    (void)sWhiteImage;

    RebuildPipeline(true, false);
    return true;
}

void Shader2D::Shutdown()
{
    if (Pipeline.Pipeline.id) { sg_destroy_pipeline(Render::ToSokol(Pipeline.Pipeline)); Pipeline.Pipeline = {}; }
    if (Shader.Shader.id) { sg_destroy_shader(Render::ToSokol(Shader.Shader)); Shader.Shader = {}; }
    if (Sampler.Sampler.id) { sg_destroy_sampler(Render::ToSokol(Sampler.Sampler)); Sampler.Sampler = {}; }
}

void Shader2D::RebuildPipeline(bool AlphaBlend, bool DepthEnabled)
{
    if (Pipeline.Pipeline.id) { sg_destroy_pipeline(Render::ToSokol(Pipeline.Pipeline)); Pipeline.Pipeline = {}; }

    sg_pipeline_desc PDesc = {};
    PDesc.shader = Render::ToSokol(Shader.Shader);
    PDesc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT2;
    PDesc.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT2;
    PDesc.layout.attrs[2].format = SG_VERTEXFORMAT_UBYTE4N;
    PDesc.index_type = SG_INDEXTYPE_UINT16;

    if (AlphaBlend)
    {
        PDesc.colors[0].blend.enabled = true;
        PDesc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
        PDesc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        PDesc.colors[0].blend.op_rgb = SG_BLENDOP_ADD;
        PDesc.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
        PDesc.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        PDesc.colors[0].blend.op_alpha = SG_BLENDOP_ADD;
    }
    PDesc.colors[0].write_mask = SG_COLORMASK_RGBA;

    if (DepthEnabled)
    {
        PDesc.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
        PDesc.depth.write_enabled = true;
    }

    Pipeline.Pipeline = Render::FromSokol(sg_make_pipeline(&PDesc));
    if (!Pipeline.Valid())
    {
        Core::Utils::Logger::Error("Shader2D", "sg_make_pipeline failed");
    }
}

void Shader2D::BindPipeline()
{
    sg_apply_pipeline(Render::ToSokol(Pipeline.Pipeline));
}

void Shader2D::BindDefaultSampler()
{
    // Caller will set up bindings externally
}

} // namespace Render
