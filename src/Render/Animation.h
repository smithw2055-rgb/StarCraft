#pragma once

#include <vector>
#include "raylib.h"

namespace Render
{

struct AnimationFrame
{
    Rectangle SourceRect;
    float Duration = 0.1f;
};

class Animation
{
public:
    void AddFrame(const AnimationFrame& Frame);
    void Update(float DeltaTime);
    Rectangle GetCurrentFrame() const;

    void Play();
    void Stop();
    void Reset();

    bool IsPlaying() const { return mIsPlaying; }

private:
    std::vector<AnimationFrame> mFrames;
    int mCurrentFrameIndex = 0;
    float mFrameTimer = 0.0f;
    bool mIsPlaying = false;
};

} // namespace Render
