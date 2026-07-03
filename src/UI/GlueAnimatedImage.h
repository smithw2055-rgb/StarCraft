#pragma once

#include <NsCore/Ptr.h>
#include <NsCore/ReflectionDeclare.h>
#include <NsGui/Image.h>

#include <string>
#include <vector>

namespace Aero
{
class BitmapImage;
}

namespace Game::Client::UI::Glue
{

class GlueAnimatedImage final : public Aero::Image
{
public:
    GlueAnimatedImage();
    ~GlueAnimatedImage();

    static void UpdateAll(double TimeSeconds);

private:
    void ConfigureFromTag();
    void SetFrame(int FrameIndex);

    bool mConfigured = false;
    std::string mFramePrefix;
    int mFrameCount = 0;
    double mFps = 12.0;
    int mLastFrame = -1;
    std::vector<Aero::Ptr<Aero::BitmapImage>> mFrames;

    NS_DECLARE_REFLECTION(GlueAnimatedImage, Aero::Image)
};

} // namespace Game::Client::UI::Glue
