#include "Render/Renderer.h"

#include "third_party/sokol/sokol_app.h"
#include "third_party/sokol/sokol_gfx.h"
#include "third_party/sokol/sokol_glue.h"
#include "third_party/sokol/sokol_log.h"

#include <iostream>

namespace Render
{

static bool g_active = false;
static sg_pass_action g_passAction = {};

static const char* BackendName(sg_backend backend)
{
    switch (backend)
    {
    case SG_BACKEND_GLCORE: return "GLCORE";
    case SG_BACKEND_GLES3: return "GLES3";
    case SG_BACKEND_D3D11: return "D3D11";
    case SG_BACKEND_METAL_IOS: return "METAL_IOS";
    case SG_BACKEND_METAL_MACOS: return "METAL_MACOS";
    case SG_BACKEND_METAL_SIMULATOR: return "METAL_SIMULATOR";
    case SG_BACKEND_WGPU: return "WGPU";
    case SG_BACKEND_DUMMY: return "DUMMY";
    default: return "UNKNOWN";
    }
}

bool Initialize()
{
    if (g_active) return true;
    sg_desc SetupDesc = {};
    SetupDesc.environment = sglue_environment();
    SetupDesc.logger.func = slog_func;
    SetupDesc.pipeline_pool_size = 256;
    SetupDesc.shader_pool_size = 64;
    SetupDesc.image_pool_size = 32768;
    SetupDesc.buffer_pool_size = 8192;
    SetupDesc.view_pool_size = 32768;
    SetupDesc.sampler_pool_size = 1024;
    SetupDesc.disable_validation = true;
    sg_setup(&SetupDesc);
    if (!sg_isvalid())
    {
        std::cerr << "Renderer: sg_setup failed!" << std::endl;
        return false;
    }
    g_passAction = {};
    g_passAction.colors[0].load_action = SG_LOADACTION_CLEAR;
    g_passAction.colors[0].clear_value.r = 0.08f;
    g_passAction.colors[0].clear_value.g = 0.08f;
    g_passAction.colors[0].clear_value.b = 0.10f;
    g_passAction.colors[0].clear_value.a = 1.0f;
    g_passAction.depth.load_action = SG_LOADACTION_CLEAR;
    g_passAction.depth.clear_value = 1.0f;
    g_active = true;
    std::cout << "Renderer: graphics backend initialized (backend=" << BackendName(sg_query_backend())
              << ", id=" << static_cast<int>(sg_query_backend()) << ")" << std::endl;
    return true;
}

void Shutdown()
{
    if (!g_active) return;
    sg_shutdown();
    g_active = false;
}

bool IsActive() { return g_active; }

void BeginFrame()
{
    sg_pass Pass = {};
    Pass.action = g_passAction;
    Pass.swapchain = sglue_swapchain();
    sg_begin_pass(&Pass);
}

void EndPass()
{
    sg_end_pass();
}

void PrepareNativeOverlay()
{
    sg_pass pass = {};
    pass.swapchain = sglue_swapchain();
    pass.action.colors[0].load_action = SG_LOADACTION_LOAD;
    pass.action.depth.load_action = SG_LOADACTION_LOAD;
    pass.action.stencil.load_action = SG_LOADACTION_LOAD;
    sg_begin_pass(&pass);
    sg_end_pass();
}

void Commit()
{
    sg_commit();
}

void EndFrame()
{
    EndPass();
    Commit();
}

void ResetStateCache()
{
    if (g_active)
        sg_reset_state_cache();
}


void SetClearColor(float R, float G, float B, float A)
{
    g_passAction.colors[0].clear_value.r = R;
    g_passAction.colors[0].clear_value.g = G;
    g_passAction.colors[0].clear_value.b = B;
    g_passAction.colors[0].clear_value.a = A;
}

} // namespace Render
