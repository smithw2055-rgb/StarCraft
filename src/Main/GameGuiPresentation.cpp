#include "Main/GameGuiPresentation.h"

#include "Client/ClientRuntime.h"
#include "Core/Platform/Platform.h"
#include "Main/GameLoop/HudResourceSync.h"
#include "Main/GameHudPresentation.h"
#include "Main/GuiRuntime.h"
#include "Main/AppRuntime.h"
#include "Main/GameSession.h"
#include "Render/Renderer.h"
#include "UI/Hud.h"
#include "third_party/sokol/sokol_app.h"
#include "third_party/sokol/sokol_gfx.h"

namespace Game::Client::GameLoop
{

namespace
{
void EnsureGuiPresentationSize(GuiRuntime& Gui, GameState State)
{
#if ENABLE_GUI_UI
    const int Width = Core::Platform::GetWindowWidth();
    const int Height = Core::Platform::GetWindowHeight();
    if (Width <= 0 || Height <= 0)
        return;

    if (Gui.AppliedWidth == Width && Gui.AppliedHeight == Height)
        return;

    Gui.AppliedWidth = Width;
    Gui.AppliedHeight = Height;
    if (State == GameState::MainMenu)
    {
        if (Gui.GlueGuiInitialized)
            Gui.GlueGui.Resize(Width, Height);
    }
    else if (Gui.MainGui.IsActive())
    {
        Gui.MainGui.Resize(Width, Height);
    }
#else
    (void)Gui;
    (void)State;
#endif
}
}

void UpdateGlueMenuPresentationLayer(GuiRuntime& Gui)
{
#if ENABLE_GUI_UI
    EnsureGuiPresentationSize(Gui, GameState::MainMenu);
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

void ResizeGuiPresentationForState(GuiRuntime& Gui, GameState State)
{
    EnsureGuiPresentationSize(Gui, State);
}

void HandleGuiPresentationEventForState(GuiRuntime& Gui, GameState State, const void* Event)
{
    const sapp_event* E = static_cast<const sapp_event*>(Event);
    if (E && E->type == SAPP_EVENTTYPE_RESIZED)
        ResizeGuiPresentationForState(Gui, State);

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
    EnsureGuiPresentationSize(Gui, GameState::InGame);
    if (Gui.UseGui && Gui.MainGui.IsActive())
    {
        SyncCampaignPingsToHud(Hud, Client, Session.GetMapData().Width, Session.GetMapData().Height);
        SyncMinimapDotsToHud(Hud, Session.Entities());
        const auto HudResources = CaptureHudResourceState(Hud);
        Gui.MainGui.SetResources(
            HudResources.MineralCount,
            HudResources.GasCount,
            HudResources.SupplyUsed,
            HudResources.SupplyMax);
        Gui.MainGui.Update(
             Session.Entities(),
             Client.GetSelectedIds(),
             Hud,
             Core::Platform::GetAbsoluteTime());
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
        const bool HandledByGui = Gui.MainGui.HandleEvent(E);
        if (E->type == SAPP_EVENTTYPE_MOUSE_DOWN ||
            E->type == SAPP_EVENTTYPE_MOUSE_UP ||
            E->type == SAPP_EVENTTYPE_MOUSE_SCROLL)
        {
            if (HandledByGui || Gui.MainGui.IsPointInHudEvent(E))
                sapp_consume_event();
        }
    }
#else
    (void)Gui;
    (void)Event;
#endif
}

} // namespace Game::Client::GameLoop
