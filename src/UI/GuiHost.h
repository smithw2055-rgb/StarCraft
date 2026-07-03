#pragma once

#include <cstdint>
#include <NsCore/Ptr.h>
#include <NsGui/IView.h>

namespace Game::Client::UI
{

class GuiHost
{
public:
    GuiHost() = default;
    ~GuiHost() { Shutdown(); }

    bool Initialize(int Width, int Height);
    void Shutdown();
    void Resize(int Width, int Height);
    void UpdateAndRender(float DeltaTimeSec);

    bool OnMouseMove(int X, int Y);
    bool OnMouseButton(int X, int Y, int Button, bool Pressed);
    bool OnMouseWheel(int X, int Y, int Delta);

    bool IsPointInHost(int X, int Y, int ViewportWidth, int ViewportHeight) const;
    bool IsActive() const { return mInitialized; }

private:
    bool CreateDefaultView();
    bool TryCreateViewFromXaml();

    bool mInitialized = false;
    bool mFailed = false;
    int mWidth = 0;
    int mHeight = 0;
    Aero::IView* mView = nullptr;
};

} // namespace Game::Client::UI
