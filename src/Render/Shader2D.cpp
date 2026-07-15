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

static const char* kHealthBarFSGLSL = R"(
#version 330 core
in vec2 v_uv;
in vec4 v_color;
out vec4 frag_color;
uniform vec4 u_solidColor;
uniform vec4 u_multiplyColor;
uniform vec4 u_teamColor;
float square(float v) {
    return v * v;
}
void main() {
    vec2 uv = abs(v_uv - vec2(0.5)) * 2.0;
    float alpha = min(1.0 - uv.y, 1.0 - square(square(square(uv.x))));
    alpha = 1.0 - square(square(1.0 - alpha));

    const float brightness = 0.50;
    vec3 gray = vec3(brightness);
    float s = step(v_uv.r, u_multiplyColor.g);
    float pixStep = step(v_uv.r, u_multiplyColor.r);
    vec3 pixColor = mix(u_teamColor.rgb, u_solidColor.rgb, pixStep);
    frag_color.rgb = mix(gray, pixColor, s) * alpha;
    frag_color.a = u_solidColor.a;
}
)";

static const char* kHealthBarFSHLSL = R"(
cbuffer HealthParams : register(b1)
{
    float4 u_solidColor;
    float4 u_multiplyColor;
    float4 u_teamColor;
};

struct PSInput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

float square(float v)
{
    return v * v;
}

float4 main(PSInput input) : SV_Target
{
    float2 uv = abs(input.uv - 0.5f) * 2.0f;
    float alpha = min(1.0f - uv.y, 1.0f - square(square(square(uv.x))));
    alpha = 1.0f - square(square(1.0f - alpha));

    const float brightness = 0.50f;
    float3 gray = float3(brightness, brightness, brightness);
    float s = step(input.uv.r, u_multiplyColor.g);
    float pixStep = step(input.uv.r, u_multiplyColor.r);
    float3 pixColor = lerp(u_teamColor.rgb, u_solidColor.rgb, pixStep);
    float4 outColor;
    outColor.rgb = lerp(gray, pixColor, s) * alpha;
    outColor.a = u_solidColor.a;
    return outColor;
}
)";

static const char* kSpriteEffectFSGLSL = R"(
#version 330 core
in vec2 v_uv;
in vec4 v_color;
out vec4 frag_color;
uniform sampler2D u_tex;
uniform sampler2D u_teamMask;
uniform vec4 u_multiplyColor;
uniform vec4 u_teamColor;
uniform vec4 u_effectData;
uniform vec4 u_effectMode;
vec3 hallucinationColor(vec3 blendTarget, vec3 sourceColor, float halT) {
    float b = dot(sourceColor, vec3(0.30196078, 0.59215686, 0.10980392));
    vec3 hallucinateColor = vec3(0.75, 1.125, 2.65) * b;
    return mix(blendTarget, hallucinateColor, halT);
}
void main() {
    vec4 sampledColor = texture(u_tex, v_uv) * v_color;
    float shadow = clamp(u_effectMode.x, 0.0, 1.0);
    if (shadow > 0.5) {
        frag_color = vec4(0.0, 0.0, 0.0, sampledColor.a * u_multiplyColor.a * 0.45);
        return;
    }

    vec4 spriteColor = sampledColor * u_multiplyColor;
    float hallucinate = clamp(u_effectData.x, 0.0, 1.0);
    float cloaked = clamp(u_effectData.y, 0.0, 1.0);
    float brighten = clamp(u_effectData.z, 0.0, 1.0);
    float useTeamColor = step(0.5, u_effectData.w);
    float warpTexture = step(0.5, u_effectMode.y);
    vec2 effectUv = mix(v_uv, v_uv * u_effectMode.zw, warpTexture);
    vec4 effectMask = texture(u_teamMask, effectUv);
    float teamMask = effectMask.r * useTeamColor;

    vec3 spriteTeamColor = spriteColor.rgb * u_teamColor.rgb;
    spriteColor.rgb = mix(spriteColor.rgb, spriteTeamColor, teamMask);

    spriteColor.rgb = hallucinationColor(spriteColor.rgb, spriteColor.rgb, hallucinate);

    vec3 b = vec3(0.30196078, 0.59215686, 0.10980392);
    float brightness = dot(b, spriteColor.rgb);
    spriteColor.rgb = mix(spriteColor.rgb, vec3(brightness), cloaked);
    spriteColor.a = mix(spriteColor.a, spriteColor.a * 0.42, cloaked);

    spriteColor.rgb = mix(spriteColor.rgb, vec3(1.0), brighten);
    if (warpTexture > 0.5) {
        spriteColor.rgb = effectMask.rgb;
        spriteColor.a = sampledColor.a * u_multiplyColor.a;
    }
    frag_color = spriteColor;
}
)";

static const char* kSpriteEffectFSHLSL = R"(
Texture2D u_tex : register(t0);
Texture2D u_teamMask : register(t1);
SamplerState u_smp : register(s0);

cbuffer SpriteEffectParams : register(b1)
{
    float4 u_multiplyColor;
    float4 u_teamColor;
    float4 u_effectData;
    float4 u_effectMode;
};

struct PSInput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

float3 HallucinationColor(float3 blendTarget, float3 sourceColor, float halT)
{
    float b = dot(sourceColor, float3(0.30196078f, 0.59215686f, 0.10980392f));
    float3 hallucinateColor = float3(0.75f, 1.125f, 2.65f) * b;
    return lerp(blendTarget, hallucinateColor, halT);
}

float4 main(PSInput input) : SV_Target
{
    float4 sampledColor = u_tex.Sample(u_smp, input.uv) * input.color;
    float shadow = saturate(u_effectMode.x);
    if (shadow > 0.5f)
    {
        return float4(0.0f, 0.0f, 0.0f, sampledColor.a * u_multiplyColor.a * 0.45f);
    }

    float4 spriteColor = sampledColor * u_multiplyColor;
    float hallucinate = saturate(u_effectData.x);
    float cloaked = saturate(u_effectData.y);
    float brighten = saturate(u_effectData.z);
    float useTeamColor = step(0.5f, u_effectData.w);
    float warpTexture = step(0.5f, u_effectMode.y);
    float2 effectUv = lerp(input.uv, input.uv * u_effectMode.zw, warpTexture);
    float4 effectMask = u_teamMask.Sample(u_smp, effectUv);
    float teamMask = effectMask.r * useTeamColor;

    float3 spriteTeamColor = spriteColor.rgb * u_teamColor.rgb;
    spriteColor.rgb = lerp(spriteColor.rgb, spriteTeamColor, teamMask);

    spriteColor.rgb = HallucinationColor(spriteColor.rgb, spriteColor.rgb, hallucinate);

    float3 b = float3(0.30196078f, 0.59215686f, 0.10980392f);
    float brightness = dot(b, spriteColor.rgb);
    spriteColor.rgb = lerp(spriteColor.rgb, float3(brightness, brightness, brightness), cloaked);
    spriteColor.a = lerp(spriteColor.a, spriteColor.a * 0.42f, cloaked);

    spriteColor.rgb = lerp(spriteColor.rgb, float3(1.0f, 1.0f, 1.0f), brighten);
    if (warpTexture > 0.5f)
    {
        spriteColor.rgb = effectMask.rgb;
        spriteColor.a = sampledColor.a * u_multiplyColor.a;
    }
    return spriteColor;
}
)";

bool Shader2D::InitTexturedShader()
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

    return true;
}

bool Shader2D::InitHealthBarShader()
{
    sg_shader_desc Desc = {};
#if defined(SOKOL_D3D11)
    Desc.vertex_func.source = kVSHLSL;
    Desc.vertex_func.entry = "main";
    Desc.vertex_func.d3d11_target = "vs_4_0";
    Desc.fragment_func.source = kHealthBarFSHLSL;
    Desc.fragment_func.entry = "main";
    Desc.fragment_func.d3d11_target = "ps_4_0";
#else
    Desc.vertex_func.source = kVSGLSL;
    Desc.vertex_func.entry = "main";
    Desc.fragment_func.source = kHealthBarFSGLSL;
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

    Desc.uniform_blocks[1].stage = SG_SHADERSTAGE_FRAGMENT;
    Desc.uniform_blocks[1].size = sizeof(HealthBarShaderParams);
#if defined(SOKOL_D3D11)
    Desc.uniform_blocks[1].hlsl_register_b_n = 1;
#else
    Desc.uniform_blocks[1].layout = SG_UNIFORMLAYOUT_STD140;
    Desc.uniform_blocks[1].msl_buffer_n = 1;
    Desc.uniform_blocks[1].wgsl_group0_binding_n = 1;
    Desc.uniform_blocks[1].glsl_uniforms[0].type = SG_UNIFORMTYPE_FLOAT4;
    Desc.uniform_blocks[1].glsl_uniforms[0].array_count = 1;
    Desc.uniform_blocks[1].glsl_uniforms[0].glsl_name = "u_solidColor";
    Desc.uniform_blocks[1].glsl_uniforms[1].type = SG_UNIFORMTYPE_FLOAT4;
    Desc.uniform_blocks[1].glsl_uniforms[1].array_count = 1;
    Desc.uniform_blocks[1].glsl_uniforms[1].glsl_name = "u_multiplyColor";
    Desc.uniform_blocks[1].glsl_uniforms[2].type = SG_UNIFORMTYPE_FLOAT4;
    Desc.uniform_blocks[1].glsl_uniforms[2].array_count = 1;
    Desc.uniform_blocks[1].glsl_uniforms[2].glsl_name = "u_teamColor";
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

    HealthBarShader.Shader = Render::FromSokol(sg_make_shader(&Desc));
    if (!HealthBarShader.Valid())
    {
        Core::Utils::Logger::Error("Shader2D", "sg_make_shader failed for health bar");
        return false;
    }

    return true;
}

bool Shader2D::InitSpriteEffectShader()
{
    sg_shader_desc Desc = {};
#if defined(SOKOL_D3D11)
    Desc.vertex_func.source = kVSHLSL;
    Desc.vertex_func.entry = "main";
    Desc.vertex_func.d3d11_target = "vs_4_0";
    Desc.fragment_func.source = kSpriteEffectFSHLSL;
    Desc.fragment_func.entry = "main";
    Desc.fragment_func.d3d11_target = "ps_4_0";
#else
    Desc.vertex_func.source = kVSGLSL;
    Desc.vertex_func.entry = "main";
    Desc.fragment_func.source = kSpriteEffectFSGLSL;
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

    Desc.uniform_blocks[1].stage = SG_SHADERSTAGE_FRAGMENT;
    Desc.uniform_blocks[1].size = sizeof(SpriteEffectShaderParams);
#if defined(SOKOL_D3D11)
    Desc.uniform_blocks[1].hlsl_register_b_n = 1;
#else
    Desc.uniform_blocks[1].layout = SG_UNIFORMLAYOUT_STD140;
    Desc.uniform_blocks[1].msl_buffer_n = 1;
    Desc.uniform_blocks[1].wgsl_group0_binding_n = 1;
    Desc.uniform_blocks[1].glsl_uniforms[0].type = SG_UNIFORMTYPE_FLOAT4;
    Desc.uniform_blocks[1].glsl_uniforms[0].array_count = 1;
    Desc.uniform_blocks[1].glsl_uniforms[0].glsl_name = "u_multiplyColor";
    Desc.uniform_blocks[1].glsl_uniforms[1].type = SG_UNIFORMTYPE_FLOAT4;
    Desc.uniform_blocks[1].glsl_uniforms[1].array_count = 1;
    Desc.uniform_blocks[1].glsl_uniforms[1].glsl_name = "u_teamColor";
    Desc.uniform_blocks[1].glsl_uniforms[2].type = SG_UNIFORMTYPE_FLOAT4;
    Desc.uniform_blocks[1].glsl_uniforms[2].array_count = 1;
    Desc.uniform_blocks[1].glsl_uniforms[2].glsl_name = "u_effectData";
    Desc.uniform_blocks[1].glsl_uniforms[3].type = SG_UNIFORMTYPE_FLOAT4;
    Desc.uniform_blocks[1].glsl_uniforms[3].array_count = 1;
    Desc.uniform_blocks[1].glsl_uniforms[3].glsl_name = "u_effectMode";
#endif

    Desc.views[0].texture.stage = SG_SHADERSTAGE_FRAGMENT;
    Desc.views[0].texture.image_type = SG_IMAGETYPE_2D;
    Desc.views[0].texture.sample_type = SG_IMAGESAMPLETYPE_FLOAT;
    Desc.views[0].texture.multisampled = false;
#if defined(SOKOL_D3D11)
    Desc.views[0].texture.hlsl_register_t_n = 0;
#endif
    Desc.views[1].texture.stage = SG_SHADERSTAGE_FRAGMENT;
    Desc.views[1].texture.image_type = SG_IMAGETYPE_2D;
    Desc.views[1].texture.sample_type = SG_IMAGESAMPLETYPE_FLOAT;
    Desc.views[1].texture.multisampled = false;
#if defined(SOKOL_D3D11)
    Desc.views[1].texture.hlsl_register_t_n = 1;
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
    Desc.texture_sampler_pairs[1].stage = SG_SHADERSTAGE_FRAGMENT;
    Desc.texture_sampler_pairs[1].view_slot = 1;
    Desc.texture_sampler_pairs[1].sampler_slot = 0;
#if !defined(SOKOL_D3D11)
    Desc.texture_sampler_pairs[1].glsl_name = "u_teamMask";
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

    SpriteEffectShader.Shader = Render::FromSokol(sg_make_shader(&Desc));
    if (!SpriteEffectShader.Valid())
    {
        Core::Utils::Logger::Error("Shader2D", "sg_make_shader failed for sprite effect");
        return false;
    }

    return true;
}

bool Shader2D::Init()
{
    if (!InitTexturedShader()) return false;
    if (!InitHealthBarShader()) return false;
    if (!InitSpriteEffectShader()) return false;

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
    if (SpriteEffectPipeline.Pipeline.id) { sg_destroy_pipeline(Render::ToSokol(SpriteEffectPipeline.Pipeline)); SpriteEffectPipeline.Pipeline = {}; }
    if (HealthBarPipeline.Pipeline.id) { sg_destroy_pipeline(Render::ToSokol(HealthBarPipeline.Pipeline)); HealthBarPipeline.Pipeline = {}; }
    if (Pipeline.Pipeline.id) { sg_destroy_pipeline(Render::ToSokol(Pipeline.Pipeline)); Pipeline.Pipeline = {}; }
    if (SpriteEffectShader.Shader.id) { sg_destroy_shader(Render::ToSokol(SpriteEffectShader.Shader)); SpriteEffectShader.Shader = {}; }
    if (HealthBarShader.Shader.id) { sg_destroy_shader(Render::ToSokol(HealthBarShader.Shader)); HealthBarShader.Shader = {}; }
    if (Shader.Shader.id) { sg_destroy_shader(Render::ToSokol(Shader.Shader)); Shader.Shader = {}; }
    if (Sampler.Sampler.id) { sg_destroy_sampler(Render::ToSokol(Sampler.Sampler)); Sampler.Sampler = {}; }
}

void Shader2D::RebuildPipelineFor(GpuShaderHandle ShaderHandle,
                                  GpuPipelineHandle& PipelineHandle,
                                  bool AlphaBlend,
                                  bool DepthEnabled)
{
    if (PipelineHandle.Pipeline.id) { sg_destroy_pipeline(Render::ToSokol(PipelineHandle.Pipeline)); PipelineHandle.Pipeline = {}; }

    sg_pipeline_desc PDesc = {};
    PDesc.shader = Render::ToSokol(ShaderHandle.Shader);
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

    PipelineHandle.Pipeline = Render::FromSokol(sg_make_pipeline(&PDesc));
    if (!PipelineHandle.Valid())
    {
        Core::Utils::Logger::Error("Shader2D", "sg_make_pipeline failed");
    }
}

void Shader2D::RebuildPipeline(bool AlphaBlend, bool DepthEnabled)
{
    RebuildPipelineFor(Shader, Pipeline, AlphaBlend, DepthEnabled);
    RebuildPipelineFor(HealthBarShader, HealthBarPipeline, AlphaBlend, DepthEnabled);
    RebuildPipelineFor(SpriteEffectShader, SpriteEffectPipeline, AlphaBlend, DepthEnabled);
}

void Shader2D::BindPipeline()
{
    sg_apply_pipeline(Render::ToSokol(Pipeline.Pipeline));
}

void Shader2D::BindHealthBarPipeline()
{
    sg_apply_pipeline(Render::ToSokol(HealthBarPipeline.Pipeline));
}

void Shader2D::BindSpriteEffectPipeline()
{
    sg_apply_pipeline(Render::ToSokol(SpriteEffectPipeline.Pipeline));
}

void Shader2D::ApplyHealthBarParams(const HealthBarShaderParams& Params)
{
    sg_apply_uniforms(1, { &Params, sizeof(Params) });
}

void Shader2D::ApplySpriteEffectParams(const SpriteEffectShaderParams& Params)
{
    sg_apply_uniforms(1, { &Params, sizeof(Params) });
}

void Shader2D::BindDefaultSampler()
{
    // Caller will set up bindings externally
}

} // namespace Render
