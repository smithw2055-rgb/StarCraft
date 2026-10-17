#include "Main/GameGuiBootstrap.h"

#include <memory>
#include <utility>

#include "Core/Utils/Logger.h"
#include "Main/GuiRuntime.h"

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
    AssetRoot = "C://Projects//StarCraft//assets";
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
