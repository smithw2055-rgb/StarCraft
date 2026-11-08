#pragma once

#include "Render/GpuTextureHandle.h"
#include "third_party/sokol/sokol_gfx.h"

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
