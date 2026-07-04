#pragma once

namespace Core::Platform
{

struct MouseScale
{
    float X = 1.0f;
    float Y = 1.0f;
};

inline MouseScale ComputeMouseScale(int WindowWidth,
                                    int WindowHeight,
                                    int FramebufferWidth,
                                    int FramebufferHeight,
                                    float DpiFallback)
{
    MouseScale Scale;
    Scale.X = (WindowWidth > 0 && FramebufferWidth > 0)
        ? (float)FramebufferWidth / (float)WindowWidth
        : DpiFallback;
    Scale.Y = (WindowHeight > 0 && FramebufferHeight > 0)
        ? (float)FramebufferHeight / (float)WindowHeight
        : DpiFallback;
    if (Scale.X <= 0.0f) Scale.X = 1.0f;
    if (Scale.Y <= 0.0f) Scale.Y = 1.0f;
    return Scale;
}

inline float MouseCoordToFramebuffer(float Coord, float Scale)
{
    return Coord * (Scale > 0.0f ? Scale : 1.0f);
}

}
