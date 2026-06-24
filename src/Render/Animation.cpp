#include "Render/Animation.h"

namespace Render
{

void Animation::AddFrame(const AnimationFrame& Frame)
{
    mFrames.push_back(Frame);
}

void Animation::Update(float DeltaTime)
{
    if (!mIsPlaying || mFrames.empty())
        return;

    mFrameTimer += DeltaTime;
    if (mFrameTimer >= mFrames[mCurrentFrameIndex].Duration)
    {
        mFrameTimer = 0.0f;
        mCurrentFrameIndex = (mCurrentFrameIndex + 1) % mFrames.size();
    }
}

Rectangle Animation::GetCurrentFrame() const
{
    if (mFrames.empty())
        return {};
    return mFrames[mCurrentFrameIndex].SourceRect;
}

void Animation::Play()
{
    mIsPlaying = true;
}

void Animation::Stop()
{
    mIsPlaying = false;
}

void Animation::Reset()
{
    mCurrentFrameIndex = 0;
    mFrameTimer = 0.0f;
}

} // namespace Render
