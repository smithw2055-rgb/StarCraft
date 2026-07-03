#include "GlueAnimatedImage.h"

#include <NsCore/Boxing.h>
#include <NsCore/Ptr.h>
#include <NsCore/ReflectionImplement.h>
#include <NsCore/String.h>
#include <NsGui/BitmapImage.h>
#include <NsGui/Uri.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
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

        Image->ConfigureFromTag();
        if (!Image->mConfigured || Image->mFrameCount <= 0)
            continue;

        const int Frame = static_cast<int>(TimeSeconds * Image->mFps) % Image->mFrameCount;
        Image->SetFrame(Frame);
    }
}

void GlueAnimatedImage::ConfigureFromTag()
{
    if (mConfigured)
        return;

    Aero::BaseComponent* Tag = GetTag();
    if (!Aero::Boxing::CanUnbox<Aero::String>(Tag))
        return;

    Aero::String Text = Aero::Boxing::Unbox<Aero::String>(Tag);
    if (!ParseAnimationTag(Text.Str(), mFramePrefix, mFrameCount, mFps))
        return;

    mFrames.resize(static_cast<size_t>(mFrameCount));
    mConfigured = true;
}

void GlueAnimatedImage::SetFrame(int FrameIndex)
{
    if (FrameIndex == mLastFrame || FrameIndex < 0 || FrameIndex >= mFrameCount)
        return;

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

NS_IMPLEMENT_REFLECTION(GlueAnimatedImage, "UI.Glue.GlueAnimatedImage")
{
    (void)helper;
}

} // namespace Game::Client::UI::Glue
