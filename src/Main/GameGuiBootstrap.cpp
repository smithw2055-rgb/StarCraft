#include "Main/GameGuiBootstrap.h"

#include <utility>

#include "Core/Utils/Logger.h"
#include "Main/GuiRuntime.h"
#include "third_party/sokol/sokol_gfx.h"

#if ENABLE_GUI_UI
#include <NsApp/ApplicationLauncher.h>
#include <NsApp/LocalFontProvider.h>
#include <NsApp/LocalTextureProvider.h>
#include <NsApp/LocalXamlProvider.h>
#include <NsApp/MediaElement.h>
#include <NsApp/ThemeProviders.h>
#include <NsCore/RegisterComponent.h>
#include <NsGui/IntegrationAPI.h>
#include <NsGui/Uri.h>
#if defined(SOKOL_D3D11)
#include "D3D11MediaPlayer.h"
#endif
#endif

namespace Game::Client::GameLoop
{

void InitializeGuiRuntime(
    GuiRuntime& Gui,
    std::string& AssetRoot,
    int Width,
    int Height,
    std::function<void(const std::string& Action)> GlueActionCallback)
{
    Gui.UseGui = false;

#if ENABLE_GUI_UI
    AssetRoot = "C://Projects//StarCraft//assets";
    Aero::GUI::Init();

    Gui.GuiInitialized = true;
    AeroApp::Launcher::RegisterAppComponents();
#if defined(SOKOL_D3D11)
    AeroApp::D3D11MediaPlayer::Init();
    AeroApp::MediaElement::SetCreateMediaPlayerCallback(
        &AeroApp::D3D11MediaPlayer::Create,
        const_cast<void*>(sg_d3d11_device()));
    Gui.GuiD3D11MediaInitialized = true;
#endif
    Aero::GUI::SetXamlProvider(Aero::MakePtr<AeroApp::LocalXamlProvider>(AssetRoot.c_str()));
    Aero::GUI::SetTextureProvider(Aero::MakePtr<AeroApp::LocalTextureProvider>(AssetRoot.c_str()));
    Aero::GUI::SetFontProvider(Aero::MakePtr<AeroApp::LocalFontProvider>(AssetRoot.c_str()));
    AeroApp::SetThemeProviders();
    Aero::GUI::LoadApplicationResources(AeroApp::Theme::DarkBlue());

    const bool GuiHudReady = Gui.MainGui.Init(AssetRoot, Width, Height, "ui/Hud.xaml");
    Gui.UseGui = false;
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

    Gui.GlueGuiInitialized = Gui.GlueGui.Initialize("", Width, Height);
    if (Gui.GlueGuiInitialized)
        Gui.GlueGui.SetActionCallback(std::move(GlueActionCallback));
#else
    (void)AssetRoot;
    (void)Width;
    (void)Height;
    (void)GlueActionCallback;
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
#if defined(SOKOL_D3D11)
        if (Gui.GuiD3D11MediaInitialized)
        {
            AeroApp::MediaElement::SetCreateMediaPlayerCallback(nullptr, nullptr);
            AeroApp::D3D11MediaPlayer::Shutdown();
            Gui.GuiD3D11MediaInitialized = false;
        }
#endif
        Aero::GUI::Shutdown();
        Gui.GuiInitialized = false;
    }
#else
    (void)Gui;
#endif
}

} // namespace Game::Client::GameLoop
