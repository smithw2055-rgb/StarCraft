#include "GuiHost.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>

#include <NsApp/ThemeProviders.h>
#if defined(SOKOL_D3D11)
#include <NsRender/D3D11Factory.h>
#include "third_party/sokol/sokol_gfx.h"
struct ID3D11DeviceContext;
#else
#include <NsRender/GLFactory.h>
#endif
#include <NsGui/IView.h>
#include <NsGui/IntegrationAPI.h>
#include <NsGui/IRenderer.h>
#include <NsGui/Grid.h>
#include <NsGui/InputEnums.h>
#include <NsGui/Uri.h>

namespace Game::Client::UI
{
namespace
{
    constexpr int kHostPanelHeightPx = 240;

    const char* kGuiXaml = R"gui(<Grid xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" />)gui"; 
}

static Aero::MouseButton ToGuiMouseButton(int Button)
{
        switch (Button)
        {
            case 1: // SAPP_MOUSEBUTTON_LEFT
                return Aero::MouseButton_Left;
            case 2: // SAPP_MOUSEBUTTON_RIGHT
                return Aero::MouseButton_Right;
            case 4: // SAPP_MOUSEBUTTON_MIDDLE
                return Aero::MouseButton_Middle;
            default:
            return Aero::MouseButton_Count;
        }
    }

bool GuiHost::CreateDefaultView()
{
    auto xaml = Aero::GUI::ParseXaml<Aero::Grid>(kGuiXaml);
    if (!xaml)
    {
        return false;
    }

    mView = Aero::GUI::CreateView(xaml).GiveOwnership();
    if (!mView)
    {
        return false;
    }

    mView->SetFlags(Aero::RenderFlags_PPAA | Aero::RenderFlags_LCD);
    if (mWidth > 0 && mHeight > 0)
        mView->SetSize((uint32_t)mWidth, (uint32_t)mHeight);
#if defined(SOKOL_D3D11)
    auto* D3DContext = static_cast<ID3D11DeviceContext*>(const_cast<void*>(sg_d3d11_device_context()));
    mView->GetRenderer()->Init(AeroApp::D3D11Factory::CreateDevice(D3DContext, false));
#else
    mView->GetRenderer()->Init(AeroApp::GLFactory::CreateDevice(false));
#endif
    return mView->GetRenderer() != nullptr;
}

bool GuiHost::TryCreateViewFromXaml()
{
    return CreateDefaultView();
}

bool GuiHost::Initialize(int Width, int Height)
{
    if (mInitialized)
        return true;

    if (mFailed)
        return false;

    try
    {
        Aero::GUI::SetLogHandler([](const char*, uint32_t, uint32_t Level, const char*, const char* Message)
        {
            const char* Prefix[] = { "T", "D", "I", "W", "E" };
            std::cout << "[Gui/" << Prefix[std::min(Level, static_cast<uint32_t>(4))] << "] " << Message << std::endl;
        });

        Aero::GUI::Init();
        AeroApp::SetThemeProviders();
        Aero::GUI::LoadApplicationResources(AeroApp::Theme::DarkBlue());

        mWidth = std::max(1, Width);
        mHeight = std::max(1, Height);

        if (!TryCreateViewFromXaml())
        {
            mFailed = true;
            return false;
        }
    }
    catch (...)
    {
        mFailed = true;
        return false;
    }

    mInitialized = true;
    return true;
}

void GuiHost::Shutdown()
{
    mView = nullptr;
    if (mInitialized)
    {
        Aero::GUI::Shutdown();
    }
    mInitialized = false;
    mFailed = false;
    mWidth = 0;
    mHeight = 0;
}

void GuiHost::Resize(int Width, int Height)
{
    mWidth = std::max(1, Width);
    mHeight = std::max(1, Height);
    if (mView)
        mView->SetSize((uint32_t)mWidth, (uint32_t)mHeight);
}

void GuiHost::UpdateAndRender(float DeltaTimeSec)
{
    if (!mView)
        return;

    mView->Update(DeltaTimeSec);
    mView->GetRenderer()->UpdateRenderTree();
    mView->GetRenderer()->RenderOffscreen();
    mView->GetRenderer()->Render();
}

bool GuiHost::OnMouseMove(int X, int Y)
{
    if (!mView)
        return false;
    return mView->MouseMove(X, Y);
}

bool GuiHost::OnMouseButton(int X, int Y, int Button, bool Pressed)
{
    if (!mView)
        return false;
    auto Btn = ToGuiMouseButton(Button);
    if (Btn == Aero::MouseButton_Count)
        return false;

    if (Pressed)
        return mView->MouseButtonDown(X, Y, Btn);
    return mView->MouseButtonUp(X, Y, Btn);
}

bool GuiHost::OnMouseWheel(int X, int Y, int Delta)
{
    (void)X;
    if (!mView)
        return false;
    return mView->MouseWheel(X, Y, Delta);
}

bool GuiHost::IsPointInHost(int X, int Y, int ViewportWidth, int ViewportHeight) const
{
    if (!mInitialized)
        return false;
    if (ViewportWidth <= 0 || ViewportHeight <= 0)
        return false;
    int HudTop = std::max(0, ViewportHeight - kHostPanelHeightPx);
    return Y >= HudTop;
}

} // namespace Game::Client::UI
