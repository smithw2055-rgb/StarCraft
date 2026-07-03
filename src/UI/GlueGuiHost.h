#pragma once

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
};

} // namespace Game::Client::UI
