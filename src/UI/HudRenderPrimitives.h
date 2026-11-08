#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "Core/Color.h"
#include "Render/Draw2D.h"
#include "Resource/UIAssetTypes.h"
#include "UI/HudLayout.h"
#include "UI/HudText.h"

namespace Game::Client::UI
{

inline RectI InsetRect(const RectI& Rect, int InsetX, int InsetY)
{
    const int SafeInsetX = std::max(0, std::min(InsetX, std::max(0, Rect.W / 2 - 1)));
    const int SafeInsetY = std::max(0, std::min(InsetY, std::max(0, Rect.H / 2 - 1)));
    return
    {
        Rect.X + SafeInsetX,
        Rect.Y + SafeInsetY,
        std::max(1, Rect.W - SafeInsetX * 2),
        std::max(1, Rect.H - SafeInsetY * 2)
    };
}

inline void DrawContainedImage(const Resource::Graphics::GpuImage& Image,
                               const RectI& Rect,
                               Render::Color Tint)
{
    if (!Image.Valid() || Rect.W <= 0 || Rect.H <= 0 || Image.Width <= 0 || Image.Height <= 0)
        return;

    const float Scale = std::min((float)Rect.W / (float)Image.Width,
                                 (float)Rect.H / (float)Image.Height);
    const float DrawW = std::max(1.0f, std::floor((float)Image.Width * Scale));
    const float DrawH = std::max(1.0f, std::floor((float)Image.Height * Scale));
    const float DrawX = (float)Rect.X + ((float)Rect.W - DrawW) * 0.5f;
    const float DrawY = (float)Rect.Y + ((float)Rect.H - DrawH) * 0.5f;
    Render::g_Draw2D.DrawSpriteFromImage(Image, DrawX, DrawY, DrawW, DrawH, Tint);
}

inline void DrawAtlasImage(const Resource::Graphics::GpuImage& Image,
                           const AtlasRect& SourceRect,
                           const RectI& DestRect,
                           Render::Color Tint)
{
    if (!Image.Valid() || Image.Width <= 0 || Image.Height <= 0 ||
        SourceRect.W <= 0 || SourceRect.H <= 0 ||
        DestRect.W <= 0 || DestRect.H <= 0)
    {
        return;
    }

    const float U0 = (float)SourceRect.X / (float)Image.Width;
    const float V0 = (float)SourceRect.Y / (float)Image.Height;
    const float U1 = (float)(SourceRect.X + SourceRect.W) / (float)Image.Width;
    const float V1 = (float)(SourceRect.Y + SourceRect.H) / (float)Image.Height;
    Render::g_Draw2D.DrawSprite(Image,
                                (float)DestRect.X, (float)DestRect.Y,
                                (float)DestRect.W, (float)DestRect.H,
                                U0, V0, U1, V1, Tint, false);
}

inline void DrawContainedAtlas(const Resource::Graphics::GpuImage& Image,
                               const AtlasRect& SourceRect,
                               const RectI& Rect,
                               Render::Color Tint)
{
    if (!Image.Valid() || Rect.W <= 0 || Rect.H <= 0 || SourceRect.W <= 0 || SourceRect.H <= 0)
        return;

    const float Scale = std::min((float)Rect.W / (float)SourceRect.W,
                                 (float)Rect.H / (float)SourceRect.H);
    const int DrawW = std::max(1, (int)std::floor((float)SourceRect.W * Scale));
    const int DrawH = std::max(1, (int)std::floor((float)SourceRect.H * Scale));
    const RectI Dest =
    {
        Rect.X + (Rect.W - DrawW) / 2,
        Rect.Y + (Rect.H - DrawH) / 2,
        DrawW,
        DrawH
    };
    DrawAtlasImage(Image, SourceRect, Dest, Tint);
}

inline RectI ExpandRectFromOpening(const RectI& OpeningRect,
                                   int SourceWidth,
                                   int SourceHeight,
                                   const RectI& OpeningInSource)
{
    if (OpeningRect.W <= 0 || OpeningRect.H <= 0 || SourceWidth <= 0 || SourceHeight <= 0 ||
        OpeningInSource.W <= 0 || OpeningInSource.H <= 0)
    {
        return OpeningRect;
    }

    const float ScaleX = (float)OpeningRect.W / (float)OpeningInSource.W;
    const float ScaleY = (float)OpeningRect.H / (float)OpeningInSource.H;
    const int OuterW = std::max(1, (int)std::lround((float)SourceWidth * ScaleX));
    const int OuterH = std::max(1, (int)std::lround((float)SourceHeight * ScaleY));
    return
    {
        OpeningRect.X - (int)std::lround((float)OpeningInSource.X * ScaleX),
        OpeningRect.Y - (int)std::lround((float)OpeningInSource.Y * ScaleY),
        OuterW,
        OuterH
    };
}

inline RectI ComputeCommandIconRect(const RectI& ButtonRect)
{
    const int InsetX = std::max(4, ButtonRect.W / 14);
    const int InsetY = std::max(4, ButtonRect.H / 14);
    const int BottomReserve = 0;
    return
    {
        ButtonRect.X + InsetX,
        ButtonRect.Y + InsetY,
        std::max(8, ButtonRect.W - InsetX * 2),
        std::max(8, ButtonRect.H - InsetY * 2 - BottomReserve)
    };
}

inline void DrawNineSlice(const Resource::Graphics::GpuImage& Image,
                          const RectI& DestRect,
                          int BorderLeft,
                          int BorderTop,
                          int BorderRight,
                          int BorderBottom,
                          Render::Color Tint)
{
    if (!Image.Valid() || DestRect.W <= 0 || DestRect.H <= 0 || Image.Width <= 0 || Image.Height <= 0)
        return;

    const int SrcLeft = std::clamp(BorderLeft, 0, std::max(0, Image.Width - 2));
    const int SrcTop = std::clamp(BorderTop, 0, std::max(0, Image.Height - 2));
    const int SrcRight = std::clamp(BorderRight, 0, std::max(0, Image.Width - SrcLeft - 1));
    const int SrcBottom = std::clamp(BorderBottom, 0, std::max(0, Image.Height - SrcTop - 1));
    const int CenterSrcW = std::max(1, Image.Width - SrcLeft - SrcRight);
    const int CenterSrcH = std::max(1, Image.Height - SrcTop - SrcBottom);

    int DstLeft = std::min(DestRect.W / 2, std::max(1, (int)std::lround((double)SrcLeft * (double)DestRect.H / (double)Image.Height)));
    int DstRight = std::min(DestRect.W - DstLeft, std::max(1, (int)std::lround((double)SrcRight * (double)DestRect.H / (double)Image.Height)));
    int DstTop = std::min(DestRect.H / 2, std::max(1, (int)std::lround((double)SrcTop * (double)DestRect.H / (double)Image.Height)));
    int DstBottom = std::min(DestRect.H - DstTop, std::max(1, (int)std::lround((double)SrcBottom * (double)DestRect.H / (double)Image.Height)));
    if (DstLeft + DstRight >= DestRect.W)
    {
        DstLeft = std::max(1, DestRect.W / 2);
        DstRight = std::max(1, DestRect.W - DstLeft);
    }
    if (DstTop + DstBottom >= DestRect.H)
    {
        DstTop = std::max(1, DestRect.H / 2);
        DstBottom = std::max(1, DestRect.H - DstTop);
    }

    const int DstCenterW = std::max(1, DestRect.W - DstLeft - DstRight);
    const int DstCenterH = std::max(1, DestRect.H - DstTop - DstBottom);

    const AtlasRect TopLeft = { 0, 0, SrcLeft, SrcTop };
    const AtlasRect TopCenter = { SrcLeft, 0, CenterSrcW, SrcTop };
    const AtlasRect TopRight = { SrcLeft + CenterSrcW, 0, SrcRight, SrcTop };
    const AtlasRect MidLeft = { 0, SrcTop, SrcLeft, CenterSrcH };
    const AtlasRect MidCenter = { SrcLeft, SrcTop, CenterSrcW, CenterSrcH };
    const AtlasRect MidRight = { SrcLeft + CenterSrcW, SrcTop, SrcRight, CenterSrcH };
    const AtlasRect BottomLeft = { 0, SrcTop + CenterSrcH, SrcLeft, SrcBottom };
    const AtlasRect BottomCenter = { SrcLeft, SrcTop + CenterSrcH, CenterSrcW, SrcBottom };
    const AtlasRect BottomRight = { SrcLeft + CenterSrcW, SrcTop + CenterSrcH, SrcRight, SrcBottom };

    const int X0 = DestRect.X;
    const int X1 = DestRect.X + DstLeft;
    const int X2 = DestRect.X + DestRect.W - DstRight;
    const int Y0 = DestRect.Y;
    const int Y1 = DestRect.Y + DstTop;
    const int Y2 = DestRect.Y + DestRect.H - DstBottom;

    DrawAtlasImage(Image, TopLeft, { X0, Y0, DstLeft, DstTop }, Tint);
    DrawAtlasImage(Image, TopCenter, { X1, Y0, DstCenterW, DstTop }, Tint);
    DrawAtlasImage(Image, TopRight, { X2, Y0, DstRight, DstTop }, Tint);
    DrawAtlasImage(Image, MidLeft, { X0, Y1, DstLeft, DstCenterH }, Tint);
    DrawAtlasImage(Image, MidCenter, { X1, Y1, DstCenterW, DstCenterH }, Tint);
    DrawAtlasImage(Image, MidRight, { X2, Y1, DstRight, DstCenterH }, Tint);
    DrawAtlasImage(Image, BottomLeft, { X0, Y2, DstLeft, DstBottom }, Tint);
    DrawAtlasImage(Image, BottomCenter, { X1, Y2, DstCenterW, DstBottom }, Tint);
    DrawAtlasImage(Image, BottomRight, { X2, Y2, DstRight, DstBottom }, Tint);
}

inline void DrawConsoleSlices(const Resource::Graphics::GpuImage& Image,
                              const RectI& DestRect,
                              Render::Color Tint)
{
    constexpr int ClassicConsoleSourceWidth = 640;
    constexpr int ClassicConsoleLeftSrcWidth = 176;
    constexpr int ClassicConsoleRightSrcWidth = 216;

    if (!Image.Valid() || DestRect.W <= 0 || DestRect.H <= 0 || Image.Width <= 0 || Image.Height <= 0)
        return;

    const float SourceWidth = (float)Image.Width;
    const float SourceHeight = (float)Image.Height;
    if (SourceWidth <= 1.0f || SourceHeight <= 1.0f)
        return;

    const float Scale = (float)DestRect.H / SourceHeight;
    const float LeftSrcW = SourceWidth * ((float)ClassicConsoleLeftSrcWidth / (float)ClassicConsoleSourceWidth);
    const float RightSrcW = SourceWidth * ((float)ClassicConsoleRightSrcWidth / (float)ClassicConsoleSourceWidth);
    const float CenterSrcW = std::max(0.0f, SourceWidth - LeftSrcW - RightSrcW);
    if (CenterSrcW <= 1.0f)
    {
        Render::g_Draw2D.DrawSpriteFromImage(Image,
                                             (float)DestRect.X, (float)DestRect.Y,
                                             (float)DestRect.W, (float)DestRect.H,
                                             Tint);
        return;
    }

    float LeftDstW = LeftSrcW * Scale;
    float RightDstW = RightSrcW * Scale;
    if (LeftDstW + RightDstW > (float)DestRect.W)
    {
        const float Shrink = (float)DestRect.W / (LeftDstW + RightDstW);
        LeftDstW *= Shrink;
        RightDstW *= Shrink;
    }
    const float CenterDstW = std::max(0.0f, (float)DestRect.W - LeftDstW - RightDstW);

    Render::g_Draw2D.DrawSprite(Image,
                                (float)DestRect.X, (float)DestRect.Y,
                                LeftDstW, (float)DestRect.H,
                                0.0f, 0.0f, LeftSrcW / SourceWidth, 1.0f,
                                Tint, false);

    if (CenterDstW > 0.0f)
    {
        Render::g_Draw2D.DrawSprite(Image,
                                    (float)DestRect.X + LeftDstW, (float)DestRect.Y,
                                    CenterDstW, (float)DestRect.H,
                                    LeftSrcW / SourceWidth, 0.0f, (LeftSrcW + CenterSrcW) / SourceWidth, 1.0f,
                                    Tint, false);
    }

    Render::g_Draw2D.DrawSprite(Image,
                                (float)DestRect.X + LeftDstW + CenterDstW, (float)DestRect.Y,
                                RightDstW, (float)DestRect.H,
                                (LeftSrcW + CenterSrcW) / SourceWidth, 0.0f, 1.0f, 1.0f,
                                Tint, false);
}

} // namespace Game::Client::UI
