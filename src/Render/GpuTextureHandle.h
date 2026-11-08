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

// === Consolidated from src/Render/Private/SokolGpuInterop.h ===
#if __has_include("third_party/sokol/sokol_gfx.h")

#ifndef SOKOL_GFX_INCLUDED
#include "third_party/sokol/sokol_gfx.h"
#endif

// Backend-only conversion helpers. Public Render headers should expose Render GPU handles, not Sokol types.
namespace Render
{

inline GpuImageHandle FromSokol(sg_image Handle) { return { Handle.id }; }
inline GpuViewHandle FromSokol(sg_view Handle) { return { Handle.id }; }
inline GpuBufferObjectHandle FromSokol(sg_buffer Handle) { return { Handle.id }; }
inline GpuShaderObjectHandle FromSokol(sg_shader Handle) { return { Handle.id }; }
inline GpuPipelineObjectHandle FromSokol(sg_pipeline Handle) { return { Handle.id }; }
inline GpuSamplerObjectHandle FromSokol(sg_sampler Handle) { return { Handle.id }; }

inline sg_image ToSokol(GpuImageHandle Handle) { return { Handle.id }; }
inline sg_view ToSokol(GpuViewHandle Handle) { return { Handle.id }; }
inline sg_buffer ToSokol(GpuBufferObjectHandle Handle) { return { Handle.id }; }
inline sg_shader ToSokol(GpuShaderObjectHandle Handle) { return { Handle.id }; }
inline sg_pipeline ToSokol(GpuPipelineObjectHandle Handle) { return { Handle.id }; }
inline sg_sampler ToSokol(GpuSamplerObjectHandle Handle) { return { Handle.id }; }

} // namespace Render

#endif
