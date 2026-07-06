#pragma once

namespace Core::Platform
{

struct MouseScale
{
    float X = 1.0f;
    float Y = 1.0f;
    float OffsetX = 0.0f;
    float OffsetY = 0.0f;
    float MaxX = 0.0f;
    float MaxY = 0.0f;
};

struct MouseCoordResult
{
    float Value = 0.0f;
    bool InsideContent = true;
};

inline MouseScale ComputeMouseScale(int WindowWidth,
                                    int WindowHeight,
                                    int FramebufferWidth,
                                    int FramebufferHeight,
                                    float DpiFallback)
{
    (void)DpiFallback;
    MouseScale Scale;
    Scale.MaxX = WindowWidth > 0 ? (float)(WindowWidth - 1) : 0.0f;
    Scale.MaxY = WindowHeight > 0 ? (float)(WindowHeight - 1) : 0.0f;

    if (WindowWidth <= 0 || WindowHeight <= 0 || FramebufferWidth <= 0 || FramebufferHeight <= 0)
        return Scale;

    // Gameplay, HUD layout, and world picking all consume logical window
    // coordinates. Sokol mouse events arrive in framebuffer pixels, while the
    // scene can be uniformly scaled inside that framebuffer when the window
    // aspect differs. Convert from framebuffer space back into the logical
    // content rect, including any letterbox / pillarbox offset.
    const float ScaleToFramebufferX = (float)FramebufferWidth / (float)WindowWidth;
    const float ScaleToFramebufferY = (float)FramebufferHeight / (float)WindowHeight;
    const float UniformScale = ScaleToFramebufferX < ScaleToFramebufferY
        ? ScaleToFramebufferX
        : ScaleToFramebufferY;

    if (UniformScale <= 0.0f)
        return Scale;

    const float ContentWidth = (float)WindowWidth * UniformScale;
    const float ContentHeight = (float)WindowHeight * UniformScale;
    Scale.OffsetX = ((float)FramebufferWidth - ContentWidth) * 0.5f;
    Scale.OffsetY = ((float)FramebufferHeight - ContentHeight) * 0.5f;
    Scale.X = 1.0f / UniformScale;
    Scale.Y = 1.0f / UniformScale;
    return Scale;
}

inline float MouseCoordToFramebuffer(float Coord, float Scale, float Offset, float MaxCoord)
{
    const float EffectiveScale = Scale > 0.0f ? Scale : 1.0f;
    const float Mapped = (Coord - Offset) * EffectiveScale;
    if (MaxCoord <= 0.0f)
        return Mapped;
    if (Mapped < 0.0f)
        return 0.0f;
    if (Mapped > MaxCoord)
        return MaxCoord;
    return Mapped;
}

inline MouseCoordResult MouseCoordToFramebufferChecked(float Coord, float Scale, float Offset, float MaxCoord)
{
    MouseCoordResult Result;
    Result.Value = MouseCoordToFramebuffer(Coord, Scale, Offset, MaxCoord);

    const float EffectiveScale = Scale > 0.0f ? Scale : 1.0f;
    if (MaxCoord > 0.0f && EffectiveScale > 0.0f)
    {
        const float ContentMin = Offset;
        const float ContentMax = Offset + (MaxCoord + 1.0f) / EffectiveScale;
        Result.InsideContent = Coord >= ContentMin && Coord < ContentMax;
    }

    return Result;
}

inline float MouseDeltaToFramebuffer(float Delta, float Scale)
{
    return Delta * (Scale > 0.0f ? Scale : 1.0f);
}

}
