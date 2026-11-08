#pragma once

#include <functional>
#include <string>

#if ENABLE_GUI_UI
// === Consolidated from src/UI/GlueGuiHost.h ===


#include <NsCore/Ptr.h>
#include <NsGui/IView.h>
#include <NsGui/UserControl.h>
#include <functional>
#include <string>

namespace Aero
{
class RenderDevice;
}

namespace Game::Client::UI::Glue
{
class GlueMenuController;
}

namespace Game::Client::UI
{

class GlueGuiHost
{
public:
    using ActionCallback = std::function<void(const std::string& Action)>;
    using AudioCallback = std::function<void(const std::string& AudioPath)>;

    GlueGuiHost() = default;
    ~GlueGuiHost() { Shutdown(); }

    bool Initialize(const std::string& AssetRoot, int Width, int Height,
                    const std::string& XamlPath = "ui/glue/GlueMenu.xaml");
    void Shutdown();
    void Resize(int Width, int Height);
    void Update(double Time);
    void RenderOffscreen();
    void Render();
    bool HandleEvent(const void* Event);

    void SetActionCallback(ActionCallback Cb);
    void SetAudioCallback(AudioCallback Cb);
    bool IsActive() const { return mInitialized; }

private:
    bool mInitialized = false;
    bool mGuiInitialized = false;
    bool mHasLastUpdateTime = false;
    double mLastUpdateTime = 0.0;

    int mWidth = 0;
    int mHeight = 0;
    float mDpiScale = 1.0f;

    std::string mAssetRoot;

    Aero::Ptr<Aero::RenderDevice> mDevice;
    Aero::Ptr<Aero::IView> mView;
    Aero::Ptr<Aero::UserControl> mRoot;
    Glue::GlueMenuController* mMenuControl = nullptr;

    ActionCallback mActionCallback;
    AudioCallback mAudioCallback;
};

} // namespace Game::Client::UI

#include "UI/MainGui.h"
#endif

namespace Game::Client { struct ClientRuntime; }
namespace Game::Client::UI { class Hud; }

namespace Game::Client::GameLoop
{

struct GameSession;
enum class GameState;

struct GuiRuntime
{
    bool UseGui = false;
    bool GuiInitialized = false;
    bool GlueGuiInitialized = false;
    int AppliedWidth = 0;
    int AppliedHeight = 0;
#if ENABLE_GUI_UI
    UI::MainGui MainGui;
    UI::GlueGuiHost GlueGui;
#endif
};

void InitializeGuiRuntime(
    GuiRuntime& Gui,
    std::string& AssetRoot,
    int Width,
    int Height,
    std::function<void(const std::string& Action)> GlueActionCallback,
    std::function<void(const std::string& AudioPath)> GlueAudioCallback = nullptr,
    bool EnableGlueMenu = true);

void ShutdownGuiRuntime(GuiRuntime& Gui);

void UpdateGlueMenuPresentationLayer(GuiRuntime& Gui);
bool UpdateGuiPresentationForState(GuiRuntime& Gui, GameState State);
void RenderGlueMenuPresentationLayer(GuiRuntime& Gui);
bool HandleGlueMenuPresentationEvent(GuiRuntime& Gui, const void* Event);
void HandleGuiPresentationEventForState(GuiRuntime& Gui, GameState State, const void* Event);

void UpdateGuiPresentationLayer(
    GuiRuntime& Gui,
    UI::Hud& Hud,
    GameSession& Session,
    ClientRuntime& Client);
void RenderGuiPresentationLayer(GuiRuntime& Gui);
void HandleGuiHudPresentationEvent(GuiRuntime& Gui, const void* Event);

} // namespace Game::Client::GameLoop
