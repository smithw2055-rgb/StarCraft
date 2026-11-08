#include "Main/GameGuiPresentation.h"

#include "Client/ClientRuntime.h"
#include "Core/Platform/Platform.h"
#include "Main/GameLoop/HudResourceSync.h"
#include "Main/GameUiPresentation.h"
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
        SyncMinimapDotsToHud(
            Hud, Session.Entities(), Session.GetLocalPlayerID(), Session.GetFogSystem());
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


// === Consolidated from src/Main/GameGuiBootstrap.cpp ===

#include <memory>
#include <utility>

#include "Core/Utils/Logger.h"

#if ENABLE_GUI_UI
#include <NsApp/ApplicationLauncher.h>
#include <NsApp/Display.h>
#include <NsApp/LocalFontProvider.h>
#include <NsApp/LocalTextureProvider.h>
#include <NsApp/LocalXamlProvider.h>
#include <NsApp/ThemeProviders.h>
#include <NsCore/RegisterComponent.h>
#include <NsGui/IntegrationAPI.h>
#include <NsGui/Uri.h>
#endif

namespace Game::Client::GameLoop
{

#if ENABLE_GUI_UI
namespace
{
class EmbeddedAudioDisplay final : public AeroApp::Display
{
public:
    void EnterMessageLoop(bool) override {}
    void* GetNativeHandle() const override { return nullptr; }
    uint32_t GetClientWidth() const override { return 0; }
    uint32_t GetClientHeight() const override { return 0; }
};

std::unique_ptr<EmbeddedAudioDisplay> gAeroAudioDisplay;

void PlayAeroAudio(void* User, const Aero::Uri& Uri, float Volume)
{
    auto* Display = static_cast<AeroApp::Display*>(User);
    if (Display)
        Display->PlayAudio(Uri, Volume);
}
}
#endif

void InitializeGuiRuntime(
    GuiRuntime& Gui,
    std::string& AssetRoot,
    int Width,
    int Height,
    std::function<void(const std::string& Action)> GlueActionCallback,
    std::function<void(const std::string& AudioPath)> GlueAudioCallback,
    bool EnableGlueMenu)
{
    Gui.UseGui = false;

#if ENABLE_GUI_UI
    Aero::GUI::Init();

    Gui.GuiInitialized = true;
    AeroApp::Launcher::RegisterAppComponents();
    gAeroAudioDisplay = std::make_unique<EmbeddedAudioDisplay>();
    Aero::GUI::SetPlayAudioCallback(gAeroAudioDisplay.get(), &PlayAeroAudio);
    Aero::GUI::SetXamlProvider(Aero::MakePtr<AeroApp::LocalXamlProvider>(AssetRoot.c_str()));
    Aero::GUI::SetTextureProvider(Aero::MakePtr<AeroApp::LocalTextureProvider>(AssetRoot.c_str()));
    Aero::GUI::SetFontProvider(Aero::MakePtr<AeroApp::LocalFontProvider>(AssetRoot.c_str()));
    AeroApp::SetThemeProviders();
    Aero::GUI::LoadApplicationResources(AeroApp::Theme::DarkBlue());

    const bool GuiHudReady = Gui.MainGui.Init(AssetRoot, Width, Height, "ui/Hud.xaml");
    Gui.UseGui = false;
    Gui.AppliedWidth = Width;
    Gui.AppliedHeight = Height;
    if (GuiHudReady)
    {
        Core::Utils::Logger::Info(
            "GameMain",
            "Gui HUD loaded; Native Terran HUD is active in-game by default (F9 toggles Gui)");
    }
    else
    {
        Core::Utils::Logger::Info(
            "GameMain",
            "Gui HUD unavailable; Native Terran HUD is active in-game");
    }

    Gui.GlueGuiInitialized = false;
    if (EnableGlueMenu)
    {
        Gui.GlueGuiInitialized = Gui.GlueGui.Initialize(AssetRoot, Width, Height);
        if (Gui.GlueGuiInitialized)
        {
            Gui.GlueGui.SetActionCallback(std::move(GlueActionCallback));
            if (GlueAudioCallback)
                Gui.GlueGui.SetAudioCallback(std::move(GlueAudioCallback));
        }
    }
    else
    {
        Core::Utils::Logger::Info("GameMain", "Glue menu skipped for direct test map startup");
    }
#else
    (void)AssetRoot;
    (void)Width;
    (void)Height;
    (void)GlueActionCallback;
    (void)GlueAudioCallback;
    (void)EnableGlueMenu;
#endif
}

void ShutdownGuiRuntime(GuiRuntime& Gui)
{
#if ENABLE_GUI_UI
    Gui.MainGui.Shutdown();
    if (Gui.GlueGuiInitialized)
        Gui.GlueGui.Shutdown();
    if (Gui.GuiInitialized)
    {
        Aero::GUI::SetPlayAudioCallback(nullptr, nullptr);
        gAeroAudioDisplay.reset();
        Aero::GUI::Shutdown();
        Gui.GuiInitialized = false;
    }
#else
    (void)Gui;
#endif
}

} // namespace Game::Client::GameLoop
