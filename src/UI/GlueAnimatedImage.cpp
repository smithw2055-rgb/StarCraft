#include "GlueAnimatedImage.h"

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
    for (size_t I = 0; I + 3 < Rgba.size(); I += 4)
    {
        const uint8_t A = Rgba[I + 3];
        Rgba[I + 0] = (uint8_t)(((uint32_t)Rgba[I + 0] * (uint32_t)A) / 255u);
        Rgba[I + 1] = (uint8_t)(((uint32_t)Rgba[I + 1] * (uint32_t)A) / 255u);
        Rgba[I + 2] = (uint8_t)(((uint32_t)Rgba[I + 2] * (uint32_t)A) / 255u);
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
        mCurrentWebmFrame = std::move(Result.FirstFrame);
        mCurrentWebmFrameIndex = Result.FrameIndex;
        mHasCurrentWebmFrame = true;
        mTextureDirty = true;
    }
    mLastFrame = Result.FrameIndex;

    if (!Result.InitialLoad)
        return;

    mFrameCount = Result.FrameCount;
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
