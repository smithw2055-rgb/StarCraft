#pragma once

#include <NsCore/Ptr.h>
#include <NsCore/ReflectionDeclare.h>
#include <NsGui/Image.h>

#include "Resource/PortraitVideoDecoder.h"

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace Aero
{
class BitmapImage;
class DynamicTextureSource;
class RenderDevice;
class Texture;
}

namespace Game::Client::UI::Glue
{

namespace Detail
{
struct AsyncWebmLoadState;
}

class GlueAnimatedImage final : public Aero::Image
{
public:
    GlueAnimatedImage();
    ~GlueAnimatedImage();

    static void UpdateAll(double TimeSeconds);
    static void SetAssetRoot(std::string AssetRoot);

    bool SetWebmSource(const std::string& RelativeOrAbsolutePath, double Fps = 12.0);
    void ClearVideoSource();
    bool GetPlaybackActive() const { return mPlaybackActive; }
    void SetPlaybackActiveProperty(bool Active);
    void SetPlaybackActive(bool Active, bool Restart = false);
    void RestartPlayback();

private:
    void ConfigureFromTag();
    void SetFrame(int FrameIndex);
    void SetPngFrame(int FrameIndex);
    void SetWebmFrame(int FrameIndex);
    void UpdatePlayback(double TimeSeconds);
    void ApplyPendingWebmLoad();
    void CancelPendingWebmLoad();
    bool DecodeWebmFrame(int FrameIndex);
    Aero::Texture* RenderDynamicTexture(Aero::RenderDevice* Device);
    static Aero::Texture* RenderDynamicTextureCallback(Aero::RenderDevice* Device, void* User);

    enum class Mode
    {
        None,
        PngSequence,
        Webm
    };

    Mode mMode = Mode::None;
    bool mConfigured = false;
    bool mPlaybackActive = true;
    std::string mFramePrefix;
    int mFrameCount = 0;
    double mFps = 12.0;
    double mPlaybackStartTime = 0.0;
    int mLastFrame = -1;
    int mCurrentWebmFrameIndex = -1;
    bool mHasCurrentWebmFrame = false;
    bool mTextureDirty = false;
    std::vector<Aero::Ptr<Aero::BitmapImage>> mFrames;
    std::vector<Resource::Graphics::DecodedVideoFrame> mCachedWebmFrames;
    Resource::Graphics::WebmVideoDecoder mWebmDecoder;
    Resource::Graphics::DecodedVideoFrame mCurrentWebmFrame;
    Aero::Ptr<Aero::DynamicTextureSource> mDynamicSource;
    Aero::Ptr<Aero::Texture> mDynamicTexture;
    // DynamicTextureSource can invoke its callback from the render thread
    // while the UI thread swaps portrait frames during selection changes.
    mutable std::mutex mDynamicTextureMutex;
    std::shared_ptr<Detail::AsyncWebmLoadState> mAsyncWebmLoadState;

    NS_DECLARE_REFLECTION(GlueAnimatedImage, Aero::Image)
};

} // namespace Game::Client::UI::Glue
