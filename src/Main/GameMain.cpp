#include "third_party/sokol/sokol_app.h"
#include "third_party/sokol/sokol_gfx.h"
#include "third_party/sokol/sokol_glue.h"
#include "third_party/sokol/sokol_log.h"

#include "Core/Platform/Platform.h"
#include "GameController.h"

#include <cstdlib>
#include <cstring>
#include <string>

namespace
{
    Game::Client::GameLoop::GameController gController;
    std::string gMapPathOverride;
    bool gHasCameraOverride = false;
    float gCameraX = 0.0f;
    float gCameraY = 0.0f;
    bool gEnableGuiHud = false;
    bool gHighDpi = true;
    int gExitAfterFrame = -1;
    int gFrameCount = 0;
}

static void InitCb()
{
    gController.Initialize(sapp_width(), sapp_height());
}

static void FrameCb()
{
    float Dt = static_cast<float>(sapp_frame_duration());
    int W = sapp_width();
    int H = sapp_height();

    gController.Update(Dt, W, H);
    gController.Render(W, H);

    if (gExitAfterFrame > 0)
    {
        ++gFrameCount;
        if (gFrameCount >= gExitAfterFrame)
            Core::Platform::RequestQuit();
    }
}

static void EventCb(const sapp_event* Event)
{
    gController.OnEvent(Event);
}

static void CleanupCb()
{
    gController.Shutdown();
}

sapp_desc sokol_main(int argc, char* argv[])
{
    for (int I = 1; I < argc; ++I)
    {
        if (std::strcmp(argv[I], "--map") == 0 && I + 1 < argc)
            gMapPathOverride = argv[++I];
        else if (std::strcmp(argv[I], "--camera") == 0 && I + 2 < argc)
        {
            gHasCameraOverride = true;
            gCameraX = std::strtof(argv[++I], nullptr);
            gCameraY = std::strtof(argv[++I], nullptr);
        }
        else if (std::strcmp(argv[I], "--gui-hud") == 0)
        {
            gEnableGuiHud = true;
        }
        else if (std::strcmp(argv[I], "--low-dpi") == 0)
        {
            gHighDpi = false;
        }
        else if (std::strcmp(argv[I], "--exit-after-frame") == 0 && I + 1 < argc)
        {
            gExitAfterFrame = std::atoi(argv[++I]);
        }
        else if (gMapPathOverride.empty())
            gMapPathOverride = argv[I];
    }
    if (gExitAfterFrame <= 0)
    {
        if (const char* EnvExitFrame = std::getenv("STARCRAFT_EXIT_AFTER_FRAME"))
            gExitAfterFrame = std::atoi(EnvExitFrame);
    }

    if (!gMapPathOverride.empty())
        gController.SetMapPathOverride(gMapPathOverride);
    if (gHasCameraOverride)
        gController.SetInitialCameraOverride(gCameraX, gCameraY);
    if (gEnableGuiHud)
        gController.SetInitialGuiHudEnabled(true);

    sapp_desc Desc = {};
    Desc.init_cb = InitCb;
    Desc.frame_cb = FrameCb;
    Desc.event_cb = EventCb;
    Desc.cleanup_cb = CleanupCb;
    Desc.width = 1280;
    Desc.height = 720;
    Desc.high_dpi = gHighDpi;
    Desc.sample_count = 1;
    Desc.window_title = "StarCraft";
    Desc.icon.sokol_default = true;
    Desc.logger.func = slog_func;
    return Desc;
}
