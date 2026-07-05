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
    (void)WindowWidth;
    (void)WindowHeight;
    (void)FramebufferWidth;
    (void)FramebufferHeight;
    (void)DpiFallback;
    // Sokol reports sapp_event.mouse_x/y in framebuffer pixels. Rendering,
    // camera, HUD, and selection also use framebuffer pixels, so applying the
    // window-to-framebuffer ratio here double-scales the pointer on high DPI.
    MouseScale Scale;
    Scale.X = 1.0f;
    Scale.Y = 1.0f;
    return Scale;
}

inline float MouseCoordToFramebuffer(float Coord, float Scale)
{
    return Coord * (Scale > 0.0f ? Scale : 1.0f);
}

}
