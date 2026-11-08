#include "Main/GameGuiPresentation.h"
// GlueComponentRegistration consolidated into GlueMenuController.h
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
        if (mAudioCallback)
            mMenuControl->SetAudioCallback(mAudioCallback);
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

void GlueGuiHost::SetAudioCallback(AudioCallback Cb)
{
    mAudioCallback = std::move(Cb);
    if (mMenuControl)
        mMenuControl->SetAudioCallback(mAudioCallback);
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
    if (mMenuControl)
        mMenuControl->Update(Time);
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


// === Consolidated from src/UI/GlueAnimatedImage.cpp ===

#include <NsCore/Boxing.h>
#include <NsCore/Ptr.h>
#include <NsCore/ReflectionImplement.h>
#include <NsCore/String.h>
#include <NsGui/BitmapImage.h>
#include <NsGui/DynamicTextureSource.h>
#include <NsGui/Uri.h>
#include <NsRender/RenderDevice.h>
#include <NsRender/Texture.h>

#include <algorithm>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace Game::Client::UI::Glue
{
namespace Detail
{
struct AsyncWebmLoadResult
{
    Resource::Graphics::WebmVideoDecoder Decoder;
    Resource::Graphics::DecodedVideoFrame FirstFrame;
    std::filesystem::path Path;
    int FrameCount = 0;
    int FrameIndex = 0;
    double Fps = 12.0;
    bool Success = false;
    bool InitialLoad = true;
};

struct AsyncWebmLoadState
{
    std::mutex Mutex;
    uint64_t Generation = 0;
    bool InFlight = false;
    bool Ready = false;
    AsyncWebmLoadResult Result;
};
}

namespace
{
class AsyncWebmLoader
{
public:
    AsyncWebmLoader()
    {
        constexpr int WorkerCount = 2;
        for (int I = 0; I < WorkerCount; ++I)
            mWorkers.emplace_back([this]() { WorkerLoop(); });
    }

    ~AsyncWebmLoader()
    {
        {
            std::lock_guard<std::mutex> Lock(mMutex);
            mStopping = true;
        }
        mWake.notify_all();
        for (auto& Worker : mWorkers)
            if (Worker.joinable())
                Worker.join();
    }

    void Submit(std::function<void()> Task)
    {
        {
            std::lock_guard<std::mutex> Lock(mMutex);
            // Favor the most recent UI request so a newly selected portrait
            // is not stuck behind every menu animation queued at startup.
            mTasks.push_front(std::move(Task));
        }
        mWake.notify_one();
    }

private:
    void WorkerLoop()
    {
        for (;;)
        {
            std::function<void()> Task;
            {
                std::unique_lock<std::mutex> Lock(mMutex);
                mWake.wait(Lock, [this]() { return mStopping || !mTasks.empty(); });
                if (mStopping && mTasks.empty())
                    return;
                Task = std::move(mTasks.front());
                mTasks.pop_front();
            }
            Task();
        }
    }

    std::mutex mMutex;
    std::condition_variable mWake;
    std::deque<std::function<void()>> mTasks;
    std::vector<std::thread> mWorkers;
    bool mStopping = false;
};

AsyncWebmLoader& WebmLoader()
{
    static AsyncWebmLoader Loader;
    return Loader;
}

std::vector<GlueAnimatedImage*>& Instances()
{
    static std::vector<GlueAnimatedImage*> sInstances;
    return sInstances;
}

std::string& AssetRoot()
{
    static std::string Root;
    return Root;
}

bool ParseAnimationTag(const char* Tag, std::string& Prefix, int& FrameCount, double& Fps)
{
    if (!Tag || !*Tag)
        return false;

    std::string Text(Tag);
    const size_t FirstSep = Text.find('|');
    const size_t SecondSep = FirstSep == std::string::npos ? std::string::npos : Text.find('|', FirstSep + 1);
    if (FirstSep == std::string::npos || SecondSep == std::string::npos)
        return false;

    Prefix = Text.substr(0, FirstSep);
    FrameCount = std::max(1, std::atoi(Text.substr(FirstSep + 1, SecondSep - FirstSep - 1).c_str()));
    Fps = std::atof(Text.substr(SecondSep + 1).c_str());
    if (Fps <= 0.0)
        Fps = 12.0;

    return !Prefix.empty();
}

bool ParseWebmTag(const char* Tag, std::string& RelativePath, double& Fps)
{
    if (!Tag || !*Tag)
        return false;

    std::string Text(Tag);
    static constexpr const char* Prefix = "webm:";
    if (Text.rfind(Prefix, 0) != 0)
        return false;

    Text = Text.substr(std::char_traits<char>::length(Prefix));
    const size_t Sep = Text.find('|');
    RelativePath = Sep == std::string::npos ? Text : Text.substr(0, Sep);
    Fps = Sep == std::string::npos ? 0.0 : std::atof(Text.substr(Sep + 1).c_str());
    if (Fps <= 0.0)
        Fps = 12.0;
    return !RelativePath.empty();
}

std::filesystem::path ResolveWebmPath(const std::string& RelativePath)
{
    std::filesystem::path Path(RelativePath);
    if (Path.is_absolute())
        return Path;

    const std::filesystem::path Root(AssetRoot());
    if (!Root.empty())
        return Root / Path;
    return Path;
}

void PremultiplyRgba(std::vector<uint8_t>& Rgba)
{
    const size_t Size = Rgba.size();
    uint8_t* Pixels = Rgba.data();
    for (size_t I = 0; I + 3 < Size; I += 4)
    {
        const uint32_t A = Pixels[I + 3];
        if (A == 255)
            continue;
        if (A == 0)
        {
            Pixels[I + 0] = 0;
            Pixels[I + 1] = 0;
            Pixels[I + 2] = 0;
            continue;
        }
        Pixels[I + 0] = (uint8_t)(((uint32_t)Pixels[I + 0] * A) / 255u);
        Pixels[I + 1] = (uint8_t)(((uint32_t)Pixels[I + 1] * A) / 255u);
        Pixels[I + 2] = (uint8_t)(((uint32_t)Pixels[I + 2] * A) / 255u);
    }
}
}

GlueAnimatedImage::GlueAnimatedImage()
    : mAsyncWebmLoadState(std::make_shared<Detail::AsyncWebmLoadState>())
{
    Instances().push_back(this);
    SetStretch(Aero::Stretch_Fill);
}

GlueAnimatedImage::~GlueAnimatedImage()
{
    // Detach the render callback while this object is still fully alive. This
    // must precede releasing the source and its UI-owned frame storage.
    SetSource(nullptr);
    {
        std::lock_guard<std::mutex> Lock(mDynamicTextureMutex);
        mDynamicSource.Reset();
        mDynamicTexture.Reset();
        mCurrentWebmFrame = {};
        mHasCurrentWebmFrame = false;
    }
    CancelPendingWebmLoad();
    auto& Items = Instances();
    Items.erase(std::remove(Items.begin(), Items.end(), this), Items.end());
}

void GlueAnimatedImage::UpdateAll(double TimeSeconds)
{
    for (GlueAnimatedImage* Image : Instances())
    {
        if (!Image)
            continue;

        Image->UpdatePlayback(TimeSeconds);
    }
}

void GlueAnimatedImage::SetAssetRoot(std::string Root)
{
    AssetRoot() = std::move(Root);
}

bool GlueAnimatedImage::SetWebmSource(const std::string& RelativeOrAbsolutePath, double Fps)
{
    if (RelativeOrAbsolutePath.empty())
        return false;

    if (Fps <= 0.0)
        Fps = 12.0;

    const std::filesystem::path Path = ResolveWebmPath(RelativeOrAbsolutePath);
    const auto State = mAsyncWebmLoadState;
    uint64_t Generation = 0;
    {
        std::lock_guard<std::mutex> Lock(State->Mutex);
        Generation = ++State->Generation;
        State->InFlight = true;
        State->Ready = false;
        State->Result = {};
    }

    WebmLoader().Submit([State, Generation, Path, Fps]() mutable
    {
        {
            std::lock_guard<std::mutex> Lock(State->Mutex);
            if (State->Generation != Generation)
                return;
        }

        Detail::AsyncWebmLoadResult Result;
        Result.Path = Path;
        Result.Fps = Fps;
        if (Result.Decoder.Open(Path) &&
            Result.Decoder.ReadNextFrame(Result.FirstFrame) &&
            !Result.FirstFrame.Image.Rgba.empty())
        {
            PremultiplyRgba(Result.FirstFrame.Image.Rgba);
            Result.FrameCount =
                (int)std::max<size_t>(1, Result.Decoder.EncodedFrameCount());
            Result.Success = true;
        }

        std::lock_guard<std::mutex> Lock(State->Mutex);
        if (State->Generation != Generation)
            return;
        State->Result = std::move(Result);
        State->InFlight = false;
        State->Ready = true;
    });

    // The current portrait remains visible until ApplyPendingWebmLoad swaps
    // in the decoded first frame on the UI thread.
    mConfigured = true;
    return true;
}

void GlueAnimatedImage::ApplyPendingWebmLoad()
{
    Detail::AsyncWebmLoadResult Result;
    {
        std::lock_guard<std::mutex> Lock(mAsyncWebmLoadState->Mutex);
        if (!mAsyncWebmLoadState->Ready)
            return;
        Result = std::move(mAsyncWebmLoadState->Result);
        mAsyncWebmLoadState->Ready = false;
    }

    if (!Result.Success)
    {
        if (!Result.InitialLoad)
            mWebmDecoder = std::move(Result.Decoder);
        std::cout << "[GlueAnimatedImage] Failed to decode WebM asynchronously: "
                  << Result.Path.string() << std::endl;
        return;
    }

    mWebmDecoder = std::move(Result.Decoder);
    {
        std::lock_guard<std::mutex> Lock(mDynamicTextureMutex);
        mCurrentWebmFrame = Result.FirstFrame;
        mCurrentWebmFrameIndex = Result.FrameIndex;
        mHasCurrentWebmFrame = true;
        mTextureDirty = true;
    }
    mLastFrame = Result.FrameIndex;

    if (Result.FrameIndex >= 0 && (size_t)Result.FrameIndex < mCachedWebmFrames.size())
    {
        mCachedWebmFrames[(size_t)Result.FrameIndex] = Result.FirstFrame;
    }

    if (!Result.InitialLoad)
        return;

    mFrameCount = Result.FrameCount;
    mCachedWebmFrames.resize(static_cast<size_t>(std::max(1, mFrameCount)));
    if (!mCachedWebmFrames.empty())
        mCachedWebmFrames[0] = mCurrentWebmFrame;

    mFps = Result.Fps;
    mMode = Mode::Webm;
    mPlaybackStartTime = 0.0;

    const auto& First = mCurrentWebmFrame.Image;
    // Stop callbacks from the previous selection before replacing the source.
    SetSource(nullptr);
    {
        std::lock_guard<std::mutex> Lock(mDynamicTextureMutex);
        mDynamicSource.Reset();
        mDynamicTexture.Reset();
    }
    Aero::Ptr<Aero::DynamicTextureSource> NewSource =
        *new Aero::DynamicTextureSource(
        (uint32_t)std::max(1, First.Width),
        (uint32_t)std::max(1, First.Height),
        &GlueAnimatedImage::RenderDynamicTextureCallback,
        this);
    SetSource(NewSource.GetPtr());
    {
        std::lock_guard<std::mutex> Lock(mDynamicTextureMutex);
        mDynamicSource = std::move(NewSource);
    }

    std::cout << "[GlueAnimatedImage] Loaded WebM asynchronously: "
              << Result.Path.string() << " frames=" << mFrameCount
              << " size=" << First.Width << "x" << First.Height << std::endl;
}

void GlueAnimatedImage::CancelPendingWebmLoad()
{
    if (!mAsyncWebmLoadState)
        return;
    std::lock_guard<std::mutex> Lock(mAsyncWebmLoadState->Mutex);
    ++mAsyncWebmLoadState->Generation;
    mAsyncWebmLoadState->InFlight = false;
    mAsyncWebmLoadState->Ready = false;
    mAsyncWebmLoadState->Result = {};
}

void GlueAnimatedImage::ClearVideoSource()
{
    CancelPendingWebmLoad();
    mWebmDecoder.Close();
    // DynamicTextureSource stores a raw callback user pointer. Remove it from
    // the visual before releasing any state the callback may still read.
    SetSource(nullptr);
    {
        std::lock_guard<std::mutex> Lock(mDynamicTextureMutex);
        mCurrentWebmFrame = {};
        mDynamicTexture.Reset();
        mDynamicSource.Reset();
        mHasCurrentWebmFrame = false;
        mTextureDirty = false;
    }

    mMode = Mode::None;
    mConfigured = false;
    mPlaybackActive = true;
    mFramePrefix.clear();
    mFrameCount = 0;
    mLastFrame = -1;
    mCurrentWebmFrameIndex = -1;
    mHasCurrentWebmFrame = false;
    mTextureDirty = false;
    mFrames.clear();
    mCachedWebmFrames.clear();
}

void GlueAnimatedImage::SetPlaybackActive(bool Active, bool Restart)
{
    ConfigureFromTag();
    if (Restart)
        RestartPlayback();
    mPlaybackActive = Active;
}

void GlueAnimatedImage::SetPlaybackActiveProperty(bool Active)
{
    SetPlaybackActive(Active, Active && !mPlaybackActive);
}

void GlueAnimatedImage::RestartPlayback()
{
    ConfigureFromTag();
    mPlaybackStartTime = 0.0;
    SetFrame(0);
}

void GlueAnimatedImage::ConfigureFromTag()
{
    if (mConfigured)
        return;

    Aero::BaseComponent* Tag = GetTag();
    if (!Aero::Boxing::CanUnbox<Aero::String>(Tag))
        return;

    Aero::String Text = Aero::Boxing::Unbox<Aero::String>(Tag);
    std::string WebmPath;
    if (ParseWebmTag(Text.Str(), WebmPath, mFps))
    {
        if (!SetWebmSource(WebmPath, mFps))
            mConfigured = true;
        return;
    }

    if (!ParseAnimationTag(Text.Str(), mFramePrefix, mFrameCount, mFps))
        return;

    mFrames.resize(static_cast<size_t>(mFrameCount));
    mMode = Mode::PngSequence;
    mConfigured = true;
}

void GlueAnimatedImage::SetFrame(int FrameIndex)
{
    if (FrameIndex == mLastFrame || FrameIndex < 0 || FrameIndex >= mFrameCount)
        return;

    if (mMode == Mode::Webm)
    {
        SetWebmFrame(FrameIndex);
        return;
    }

    SetPngFrame(FrameIndex);
}

void GlueAnimatedImage::UpdatePlayback(double TimeSeconds)
{
    ApplyPendingWebmLoad();
    ConfigureFromTag();
    if (!mConfigured || mFrameCount <= 0 || !mPlaybackActive)
        return;

    if (mPlaybackStartTime <= 0.0)
        mPlaybackStartTime = TimeSeconds;

    const double Elapsed = std::max(0.0, TimeSeconds - mPlaybackStartTime);
    const int Frame = static_cast<int>(Elapsed * mFps) % mFrameCount;
    SetFrame(Frame);
}

void GlueAnimatedImage::SetPngFrame(int FrameIndex)
{
    Aero::Ptr<Aero::BitmapImage>& Source = mFrames[static_cast<size_t>(FrameIndex)];
    if (!Source)
    {
        char Path[512] = {};
        std::snprintf(Path, sizeof(Path), "%s%03d.png", mFramePrefix.c_str(), FrameIndex + 1);
        Source = Aero::MakePtr<Aero::BitmapImage>(Aero::Uri(Path));
    }

    SetSource(Source.GetPtr());
    mLastFrame = FrameIndex;
}

void GlueAnimatedImage::SetWebmFrame(int FrameIndex)
{
    if (FrameIndex >= 0 && (size_t)FrameIndex < mCachedWebmFrames.size() &&
        !mCachedWebmFrames[(size_t)FrameIndex].Image.Rgba.empty())
    {
        {
            std::lock_guard<std::mutex> Lock(mDynamicTextureMutex);
            mCurrentWebmFrame = mCachedWebmFrames[(size_t)FrameIndex];
            mCurrentWebmFrameIndex = FrameIndex;
            mHasCurrentWebmFrame = true;
            mTextureDirty = true;
        }
        mLastFrame = FrameIndex;
        return;
    }

    if (!DecodeWebmFrame(FrameIndex))
        return;
    mLastFrame = FrameIndex;
}

bool GlueAnimatedImage::DecodeWebmFrame(int FrameIndex)
{
    if (FrameIndex < 0 || FrameIndex >= mFrameCount)
        return false;
    if (mHasCurrentWebmFrame && FrameIndex == mCurrentWebmFrameIndex)
        return true;

    const auto State = mAsyncWebmLoadState;
    uint64_t Generation = 0;
    {
        std::lock_guard<std::mutex> Lock(State->Mutex);
        if (State->InFlight || State->Ready)
            return false;
        Generation = State->Generation;
        State->InFlight = true;
    }

    const int StartFrameIndex = mCurrentWebmFrameIndex;
    auto Decoder = std::make_shared<Resource::Graphics::WebmVideoDecoder>(
        std::move(mWebmDecoder));
    WebmLoader().Submit(
        [State, Generation, Decoder, StartFrameIndex, FrameIndex]() mutable
    {
        Detail::AsyncWebmLoadResult Result;
        Result.InitialLoad = false;
        Result.FrameIndex = FrameIndex;
        int DecodedIndex = StartFrameIndex;
        bool Succeeded = true;
        if (FrameIndex <= DecodedIndex)
        {
            if (!Decoder->Rewind())
                Succeeded = false;
            else
                DecodedIndex = -1;
        }

        Resource::Graphics::DecodedVideoFrame Frame;
        while (DecodedIndex < FrameIndex)
        {
            if (!Decoder->ReadNextFrame(Frame))
            {
                if (!Decoder->Rewind() || !Decoder->ReadNextFrame(Frame))
                {
                    Succeeded = false;
                    break;
                }
                DecodedIndex = 0;
            }
            else
            {
                ++DecodedIndex;
            }
        }

        Result.Decoder = std::move(*Decoder);
        if (Succeeded && !Frame.Image.Rgba.empty())
        {
            PremultiplyRgba(Frame.Image.Rgba);
            Result.FirstFrame = std::move(Frame);
            Result.Success = true;
        }

        std::lock_guard<std::mutex> Lock(State->Mutex);
        if (State->Generation != Generation)
            return;
        State->Result = std::move(Result);
        State->InFlight = false;
        State->Ready = true;
    });
    return false;
}

Aero::Texture* GlueAnimatedImage::RenderDynamicTextureCallback(Aero::RenderDevice* Device, void* User)
{
    auto* Image = static_cast<GlueAnimatedImage*>(User);
    return Image ? Image->RenderDynamicTexture(Device) : nullptr;
}

Aero::Texture* GlueAnimatedImage::RenderDynamicTexture(Aero::RenderDevice* Device)
{
    std::lock_guard<std::mutex> Lock(mDynamicTextureMutex);
    if (!Device || !mHasCurrentWebmFrame)
        return nullptr;

    const auto& Image = mCurrentWebmFrame.Image;
    if (Image.Width <= 0 || Image.Height <= 0 || Image.Rgba.empty())
        return nullptr;

    if (!mDynamicTexture ||
        mDynamicTexture->GetWidth() != (uint32_t)Image.Width ||
        mDynamicTexture->GetHeight() != (uint32_t)Image.Height)
    {
        mDynamicTexture = Device->CreateTexture("GlueWebM", (uint32_t)Image.Width, (uint32_t)Image.Height,
                                                1, Aero::TextureFormat::RGBA8, nullptr);
        mTextureDirty = true;
    }

    if (mDynamicTexture && mTextureDirty)
    {
        Device->UpdateTexture(mDynamicTexture.GetPtr(), 0, 0, 0,
                              (uint32_t)Image.Width, (uint32_t)Image.Height,
                              Image.Rgba.data());
        mTextureDirty = false;
    }

    return mDynamicTexture.GetPtr();
}

NS_IMPLEMENT_REFLECTION(GlueAnimatedImage, "UI.Glue.GlueAnimatedImage")
{
    NsProp("PlaybackActive", &GlueAnimatedImage::GetPlaybackActive, &GlueAnimatedImage::SetPlaybackActiveProperty);
}

} // namespace Game::Client::UI::Glue
