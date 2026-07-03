#include "Main/GameGuiPresentation.h"

#include "Client/ClientRuntime.h"
#include "Core/Platform/Platform.h"
#include "Main/GameLoop/HudResourceSync.h"
#include "Main/GuiRuntime.h"
#include "Main/AppRuntime.h"
#include "Main/GameSession.h"
#include "Render/Renderer.h"
#include "UI/Hud.h"
#include "third_party/sokol/sokol_app.h"
#include "third_party/sokol/sokol_gfx.h"

namespace Game::Client::GameLoop
{

void UpdateGlueMenuPresentationLayer(GuiRuntime& Gui)
{
#if ENABLE_GUI_UI
    if (Gui.GlueGuiInitialized)
        Gui.GlueGui.Update(Core::Platform::GetAbsoluteTime());
#else
    (void)Gui;
#endif
}

bool UpdateGuiPresentationForState(GuiRuntime& Gui, GameState State)
{
    if (State == GameState::MainMenu)
    {
        UpdateGlueMenuPresentationLayer(Gui);
        return true;
    }

    return false;
}

void RenderGlueMenuPresentationLayer(GuiRuntime& Gui)
{
#if ENABLE_GUI_UI
    if (Gui.GlueGuiInitialized)
    {
        Gui.GlueGui.RenderOffscreen();
        sg_reset_state_cache();
    }
#endif

    Render::BeginFrame();

#if ENABLE_GUI_UI
    if (Gui.GlueGuiInitialized)
    {
        Gui.GlueGui.Render();
        sg_reset_state_cache();
        Render::EndFrame();
        return;
    }
#else
    (void)Gui;
#endif

    Render::EndFrame();
}

bool HandleGlueMenuPresentationEvent(GuiRuntime& Gui, const void* Event)
{
#if ENABLE_GUI_UI
    if (Gui.GlueGuiInitialized && Gui.GlueGui.HandleEvent(Event))
    {
        sapp_consume_event();
        return true;
    }
#else
    (void)Gui;
    (void)Event;
#endif
    return false;
}

void HandleGuiPresentationEventForState(GuiRuntime& Gui, GameState State, const void* Event)
{
    if (State == GameState::MainMenu)
    {
        HandleGlueMenuPresentationEvent(Gui, Event);
        return;
    }

    HandleGuiHudPresentationEvent(Gui, Event);
}

void UpdateGuiPresentationLayer(
    GuiRuntime& Gui,
    UI::Hud& Hud,
    GameSession& Session,
    ClientRuntime& Client)
{
#if ENABLE_GUI_UI
    if (Gui.UseGui && Gui.MainGui.IsActive())
    {
        const auto HudResources = CaptureHudResourceState(Hud);
        Gui.MainGui.SetResources(
            HudResources.MineralCount,
            HudResources.GasCount,
            HudResources.SupplyUsed,
            HudResources.SupplyMax);
        Gui.MainGui.Update(
            Session.Entities(),
            Client.GetSelectedIds(),
            Core::Platform::GetAbsoluteTime());
        Gui.MainGui.Resize(
            Core::Platform::GetWindowWidth(),
            Core::Platform::GetWindowHeight());
        Gui.MainGui.RenderOffscreen();
        sg_reset_state_cache();
    }
#endif
}

void RenderGuiPresentationLayer(GuiRuntime& Gui)
{
#if ENABLE_GUI_UI
    if (Gui.UseGui && Gui.MainGui.IsActive())
    {
        Gui.MainGui.Render();
    }
#endif
    sg_reset_state_cache();
    Render::EndPass();
    Render::Commit();
}

void HandleGuiHudPresentationEvent(GuiRuntime& Gui, const void* Event)
{
#if ENABLE_GUI_UI
    const sapp_event* E = static_cast<const sapp_event*>(Event);
    if (Gui.UseGui && Gui.MainGui.IsActive() && E)
    {
        Gui.MainGui.HandleEvent(E);
        if (E->type == SAPP_EVENTTYPE_MOUSE_DOWN ||
            E->type == SAPP_EVENTTYPE_MOUSE_UP ||
            E->type == SAPP_EVENTTYPE_MOUSE_SCROLL)
        {
            if (Gui.MainGui.IsPointInHud(E->mouse_x, E->mouse_y))
                sapp_consume_event();
        }
    }
#else
    (void)Gui;
    (void)Event;
#endif
}

} // namespace Game::Client::GameLoop
