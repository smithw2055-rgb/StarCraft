#include "Render/sokol/Renderer.h"

#include "third_party/sokol/sokol_app.h"
#include "third_party/sokol/sokol_glue.h"
#include "third_party/sokol/sokol_log.h"

#include <iostream>

namespace Render
{

static bool g_active = false;
static sg_pass_action g_passAction = {};

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
    std::cout << "Renderer: sokol_gfx initialized (backend=" << static_cast<int>(sg_query_backend()) << ")" << std::endl;
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

void EndFrame()
{
    sg_end_pass();
    sg_commit();
}

sg_pass_action GetDefaultPassAction() { return g_passAction; }

void SetClearColor(float R, float G, float B, float A)
{
    g_passAction.colors[0].clear_value.r = R;
    g_passAction.colors[0].clear_value.g = G;
    g_passAction.colors[0].clear_value.b = B;
    g_passAction.colors[0].clear_value.a = A;
}

} // namespace Render
