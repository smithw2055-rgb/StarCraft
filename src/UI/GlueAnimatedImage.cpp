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
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace Game::Client::UI::Glue
{
namespace
{
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
{
    Instances().push_back(this);
    SetStretch(Aero::Stretch_Fill);
}

GlueAnimatedImage::~GlueAnimatedImage()
{
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
    ClearVideoSource();

    if (RelativeOrAbsolutePath.empty())
        return false;

    if (Fps <= 0.0)
        Fps = 12.0;

    const std::filesystem::path Path = ResolveWebmPath(RelativeOrAbsolutePath);
    if (!mWebmDecoder.Open(Path))
    {
        std::cout << "[GlueAnimatedImage] Failed to decode WebM: " << Path.string() << std::endl;
        return false;
    }

    Resource::Graphics::DecodedVideoFrame FirstFrame;
    if (!mWebmDecoder.ReadNextFrame(FirstFrame) || FirstFrame.Image.Rgba.empty())
    {
        std::cout << "[GlueAnimatedImage] Failed to read first WebM frame: " << Path.string() << std::endl;
        mWebmDecoder.Close();
        return false;
    }

    PremultiplyRgba(FirstFrame.Image.Rgba);
    mCurrentWebmFrame = std::move(FirstFrame);
    mCurrentWebmFrameIndex = 0;
    mHasCurrentWebmFrame = true;
    mFrameCount = (int)std::max<size_t>(1, mWebmDecoder.EncodedFrameCount());
    mFps = Fps;
    mMode = Mode::Webm;
    mTextureDirty = true;

    const auto& First = mCurrentWebmFrame.Image;
    mDynamicSource = *new Aero::DynamicTextureSource(
        (uint32_t)std::max(1, First.Width),
        (uint32_t)std::max(1, First.Height),
        &GlueAnimatedImage::RenderDynamicTextureCallback,
        this);
    SetSource(mDynamicSource.GetPtr());

    std::cout << "[GlueAnimatedImage] Loaded WebM: " << Path.string()
              << " frames=" << mFrameCount
              << " size=" << First.Width << "x" << First.Height << std::endl;
    mConfigured = true;
    return true;
}

void GlueAnimatedImage::ClearVideoSource()
{
    mWebmDecoder.Close();
    mCurrentWebmFrame = {};
    mDynamicTexture.Reset();
    mDynamicSource.Reset();
    SetSource(nullptr);

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

    if (!mHasCurrentWebmFrame || FrameIndex <= mCurrentWebmFrameIndex)
    {
        if (!mWebmDecoder.Rewind())
            return false;
        mCurrentWebmFrameIndex = -1;
        mHasCurrentWebmFrame = false;
    }

    Resource::Graphics::DecodedVideoFrame Frame;
    while (mCurrentWebmFrameIndex < FrameIndex)
    {
        if (!mWebmDecoder.ReadNextFrame(Frame))
        {
            if (!mWebmDecoder.Rewind() || !mWebmDecoder.ReadNextFrame(Frame))
                return false;
            mCurrentWebmFrameIndex = 0;
        }
        else
        {
            ++mCurrentWebmFrameIndex;
        }

        PremultiplyRgba(Frame.Image.Rgba);
        mCurrentWebmFrame = std::move(Frame);
        mHasCurrentWebmFrame = true;
    }

    mTextureDirty = true;
    return true;
}

Aero::Texture* GlueAnimatedImage::RenderDynamicTextureCallback(Aero::RenderDevice* Device, void* User)
{
    auto* Image = static_cast<GlueAnimatedImage*>(User);
    return Image ? Image->RenderDynamicTexture(Device) : nullptr;
}

Aero::Texture* GlueAnimatedImage::RenderDynamicTexture(Aero::RenderDevice* Device)
{
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
