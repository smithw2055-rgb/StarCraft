#include "GlueGuiHost.h"
#include "GlueAnimatedImage.h"
#include "GlueComponentRegistration.h"
#include "GlueMenuController.h"
#include <algorithm>
#include <cmath>
#include <iostream>

#include <NsGui/IView.h>
#include <NsGui/IRenderer.h>
#include <NsGui/IntegrationAPI.h>
#include <NsGui/Uri.h>
#include <NsGui/InputEnums.h>
#if defined(SOKOL_D3D11)
#include <NsRender/D3D11Factory.h>
#else
#include <NsRender/GLFactory.h>
#endif

#include "third_party/sokol/sokol_app.h"
#include "third_party/sokol/sokol_gfx.h"
#include "Core/Platform/Platform.h"
#include "Core/Utils/InputDebug.h"

#if defined(SOKOL_D3D11)
struct ID3D11DeviceContext;
#endif

#include <sstream>

namespace Game::Client::UI
{
namespace
{
float EventMouseToLogical(float Coord, int WindowExtent, int FramebufferExtent, bool Horizontal)
{
    if (WindowExtent <= 0 && FramebufferExtent <= 0)
        return Coord;

    int LogicalExtent = Horizontal ? Core::Platform::GetWindowWidth() : Core::Platform::GetWindowHeight();
    int PhysicalExtent = Horizontal ? Core::Platform::GetClientWidth() : Core::Platform::GetClientHeight();
    if (LogicalExtent <= 0)
        LogicalExtent = WindowExtent;
    if (PhysicalExtent <= 0)
        PhysicalExtent = FramebufferExtent > 0 ? FramebufferExtent : WindowExtent;
    if (LogicalExtent <= 0 || PhysicalExtent <= 0)
        return Coord;
    return Coord * (float)LogicalExtent / (float)PhysicalExtent;
}
}

float GetGlueDpiScale()
{
    float Scale = sapp_dpi_scale();
    return Scale > 1.0f ? Scale : 1.0f;
}

bool GlueGuiHost::Initialize(const std::string& AssetRoot, int Width, int Height,
                              const std::string& XamlPath)
{
    if (mInitialized) return true;

    UI::Glue::RegisterGlueComponents(true);

    mAssetRoot = AssetRoot;
    UI::Glue::GlueAnimatedImage::SetAssetRoot(mAssetRoot);
    mDpiScale = GetGlueDpiScale();
    mWidth = std::max(1, Width);
    mHeight = std::max(1, Height);

    std::cout << "[GlueGui] Loading: " << XamlPath << " (" << mWidth << "x" << mHeight << ")" << std::endl;

    mRoot = Aero::GUI::LoadXaml<Aero::UserControl>(XamlPath.c_str());
    if (!mRoot)
    {
        std::cerr << "[GlueGui] FAILED to load: " << XamlPath << std::endl;
        return false;
    }
    std::cout << "[GlueGui] XAML loaded OK" << std::endl;

    mMenuControl = Aero::DynamicCast<Glue::GlueMenuController*>(mRoot.GetPtr());
    if (mMenuControl)
    {
        mMenuControl->SetActionCallback(mActionCallback);
        mMenuControl->Initialize();
        std::cout << "[GlueGui] GlueMenuController initialized" << std::endl;
    }
    else
    {
        std::cout << "[GlueGui] Root is not GlueMenuController (type=" 
                  << (mRoot.GetPtr() ? mRoot->GetClassType()->GetName() : "null") << ")" << std::endl;
    }

    mView = Aero::GUI::CreateView(mRoot);
    if (!mView)
    {
        std::cerr << "[GlueGui] CreateView failed" << std::endl;
        return false;
    }

    mView->SetFlags(Aero::RenderFlags_PPAA);
    mView->SetScale(mDpiScale);
    mView->SetSize(
        static_cast<uint32_t>(std::lround((float)mWidth * mDpiScale)),
        static_cast<uint32_t>(std::lround((float)mHeight * mDpiScale)));

#if defined(SOKOL_D3D11)
    auto* D3DContext = static_cast<ID3D11DeviceContext*>(const_cast<void*>(sg_d3d11_device_context()));
    mDevice = AeroApp::D3D11Factory::CreateDevice(D3DContext, false);
#else
    mDevice = AeroApp::GLFactory::CreateDevice(false);
#endif
    if (!mDevice)
    {
        std::cerr << "[GlueGui] GL render device failed" << std::endl;
        return false;
    }

    mView->GetRenderer()->Init(mDevice.GetPtr());
    mInitialized = true;

    std::cout << "[GlueGui] Ready. View size=" << mWidth << "x" << mHeight 
              << " scale=" << mDpiScale << std::endl;
    return true;
}

void GlueGuiHost::SetActionCallback(ActionCallback Cb)
{
    mActionCallback = std::move(Cb);
    if (mMenuControl)
        mMenuControl->SetActionCallback(mActionCallback);
}

void GlueGuiHost::Shutdown()
{
    if (mInitialized && mView)
        mView->GetRenderer()->Shutdown();
    mInitialized = false;
    mMenuControl = nullptr;
    mDevice.Reset();
    mRoot.Reset();
    mView.Reset();
}

void GlueGuiHost::Resize(int Width, int Height)
{
    if (!mView) return;
    mWidth = std::max(1, Width);
    mHeight = std::max(1, Height);
    mDpiScale = GetGlueDpiScale();
    mView->SetScale(mDpiScale);
    mView->SetSize(
        static_cast<uint32_t>(std::lround((float)mWidth * mDpiScale)),
        static_cast<uint32_t>(std::lround((float)mHeight * mDpiScale)));
}

void GlueGuiHost::Update(double Time)
{
    if (!mInitialized || !mView) return;
    UI::Glue::GlueAnimatedImage::UpdateAll(Time);
    mView->Update(Time);
}

void GlueGuiHost::Render()
{
    if (!mInitialized || !mView) return;

    auto* Renderer = mView->GetRenderer();
    if (!Renderer) return;
    Renderer->Render();
}

void GlueGuiHost::RenderOffscreen()
{
    if (!mInitialized || !mView) return;

    auto* Renderer = mView->GetRenderer();
    Renderer->UpdateRenderTree();
    Renderer->RenderOffscreen();
}

bool GlueGuiHost::HandleEvent(const void* EventPtr)
{
    if (!mInitialized || !mView || !EventPtr) return false;

    const sapp_event* E = static_cast<const sapp_event*>(EventPtr);
    const float LogicalX = EventMouseToLogical(E->mouse_x, E->window_width, E->framebuffer_width, true);
    const float LogicalY = EventMouseToLogical(E->mouse_y, E->window_height, E->framebuffer_height, false);
    const float X = LogicalX * mDpiScale;
    const float Y = LogicalY * mDpiScale;
    const int ViewX = static_cast<int>(std::lround(X));
    const int ViewY = static_cast<int>(std::lround(Y));
    const bool ShouldLog =
        E->type == SAPP_EVENTTYPE_MOUSE_DOWN ||
        E->type == SAPP_EVENTTYPE_MOUSE_UP ||
        (E->type == SAPP_EVENTTYPE_MOUSE_MOVE && Core::Utils::InputDebugShouldLogMove());
    bool Handled = false;

    switch (E->type)
    {
    case SAPP_EVENTTYPE_MOUSE_DOWN:
    {
        Aero::MouseButton Btn = Aero::MouseButton_Left;
        if (E->mouse_button == SAPP_MOUSEBUTTON_RIGHT) Btn = Aero::MouseButton_Right;
        else if (E->mouse_button == SAPP_MOUSEBUTTON_MIDDLE) Btn = Aero::MouseButton_Middle;
        Handled = mView->MouseButtonDown(ViewX, ViewY, Btn);
        break;
    }
    case SAPP_EVENTTYPE_MOUSE_UP:
    {
        Aero::MouseButton Btn = Aero::MouseButton_Left;
        if (E->mouse_button == SAPP_MOUSEBUTTON_RIGHT) Btn = Aero::MouseButton_Right;
        else if (E->mouse_button == SAPP_MOUSEBUTTON_MIDDLE) Btn = Aero::MouseButton_Middle;
        if (mView->MouseButtonUp(ViewX, ViewY, Btn))
            Handled = true;

        if (!Handled && E->mouse_button == SAPP_MOUSEBUTTON_LEFT && mMenuControl)
        {
            Handled = mMenuControl->HandleFallbackClick(LogicalX, LogicalY,
                                                        static_cast<float>(mWidth), static_cast<float>(mHeight));
        }
        break;
    }
    case SAPP_EVENTTYPE_MOUSE_MOVE:
        Handled = mView->MouseMove(ViewX, ViewY);
        break;
    case SAPP_EVENTTYPE_MOUSE_SCROLL:
        return mView->MouseWheel(ViewX, ViewY, static_cast<int>(std::lround(E->scroll_y)));
    case SAPP_EVENTTYPE_KEY_DOWN:
        return mView->KeyDown(static_cast<Aero::Key>(E->key_code));
    case SAPP_EVENTTYPE_KEY_UP:
        return mView->KeyUp(static_cast<Aero::Key>(E->key_code));
    case SAPP_EVENTTYPE_CHAR:
        return mView->Char(E->char_code);
    default:
        break;
    }
    if (ShouldLog && Core::Utils::InputDebugEnabled())
    {
        std::ostringstream Stream;
        Stream << "type=";
        switch (E->type)
        {
            case SAPP_EVENTTYPE_MOUSE_DOWN: Stream << "mouse_down"; break;
            case SAPP_EVENTTYPE_MOUSE_UP: Stream << "mouse_up"; break;
            case SAPP_EVENTTYPE_MOUSE_MOVE: Stream << "mouse_move"; break;
            default: Stream << "other"; break;
        }
        Stream << " raw=" << E->mouse_x << "," << E->mouse_y
               << " logical=" << LogicalX << "," << LogicalY
               << " view=" << ViewX << "," << ViewY
               << " dpi=" << mDpiScale
               << " handled=" << (Handled ? 1 : 0);
        Core::Utils::InputDebugLog("InputGlueGuiHost", Stream.str());
    }
    return Handled;
}

} // namespace Game::Client::UI
