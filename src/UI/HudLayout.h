#pragma once

#include <array>

namespace Game::Client::UI
{

struct LogicalCanvas
{
    int X = 0;
    int Y = 0;
    int Width = 0;
    int Height = 0;
    float Scale = 1.0f;
};

struct RectI
{
    int X = 0;
    int Y = 0;
    int W = 0;
    int H = 0;

    bool Contains(int Px, int Py) const
    {
        return Px >= X && Px < X + W && Py >= Y && Py < Y + H;
    }
};

struct HudLayout
{
    RectI ConsoleRect;
    RectI MinimapRect;
    RectI InfoFrameRect;
    RectI InfoRect;
    RectI PortraitFrameRect;
    RectI AuxiliaryRect;
    RectI PreviewRect;
    RectI QueueRect;
    std::array<RectI, 9> CommandRects = {};
    std::array<RectI, 3> MinimapButtonRects = {};

    int HudTop = 0;
    int HudHeight = 0;
};

struct AtlasRect
{
    int X = 0;
    int Y = 0;
    int W = 0;
    int H = 0;
};

struct ScvInfoLayout
{
    RectI PortraitRect;
    RectI HeaderRect;
    RectI StatIconRowRect;
    RectI HpTextRect;
    RectI HpBarRect;
    RectI FooterCardA;
    RectI FooterCardB;
};

HudLayout ResolveHudLayout(int ViewportWidth, int ViewportHeight);
inline ScvInfoLayout ResolveScvInfoLayout(const RectI& InfoRect);

} // namespace Game::Client::UI

// === Inlined from src/UI/HudLayout.cpp ===


#include <algorithm>
#include <cmath>
#include <cstddef>

namespace Game::Client::UI
{

namespace Detail
{
    constexpr int FhdLogicalWidth = 1920;
    constexpr int FhdLogicalHeight = 1080;
    constexpr RectI FhdConsoleAnchor = { 0, 760, 1920, 320 };
    constexpr RectI FhdQueueAnchor = { 1152, 918, 156, 98 };

    constexpr int TerranConsoleLeftSourceHeight = 700;
    constexpr int TerranConsoleRightSourceWidth = 2286;
    constexpr int TerranConsoleRightSourceHeight = 700;
    constexpr RectI TerranConsoleLeftMinimapOpening = { 440, 169, 522, 516 };
    constexpr int TerranConsoleCenterSourceWidth = 1156;
    constexpr int TerranConsoleCenterSourceHeight = 512;
    constexpr RectI TerranConsoleCenterOpening = { 45, 117, 1010, 393 };
    constexpr int TerranConsolePortraitSourceWidth = 256;
    constexpr int TerranConsolePortraitSourceHeight = 256;
    constexpr RectI TerranConsolePortraitOpening = { 27, 3, 207, 237 };
    constexpr std::array<RectI, 9> TerranConsoleRightCommandSlots =
    {{
        { 1318, 196, 127, 124 }, { 1507, 196, 127, 124 }, { 1694, 196, 127, 124 },
        { 1318, 373, 127, 124 }, { 1507, 373, 127, 124 }, { 1694, 373, 127, 124 },
        { 1318, 549, 127, 124 }, { 1507, 549, 127, 124 }, { 1694, 549, 127, 124 }
    }};
    constexpr int TerranCommandSlotOutsetX = 10;
    constexpr int TerranCommandSlotOutsetY = 12;
    constexpr int TerranCommandSlotYOffset = 18;
    constexpr std::array<int, 3> TerranCommandRowOffsetPx = {{ 3, -3, -8 }};
    constexpr std::array<int, 3> TerranCommandColOffsetPx = {{ 2, 1, 0 }};
    constexpr int TerranCommandWidthAdjustPx = 1;
    constexpr int TerranCommandHeightAdjustPx = -1;
    constexpr std::size_t TerranPortraitAnchorSlotIndex = 3;
    constexpr int TerranPortraitAnchorOffsetX = -380;
    constexpr int TerranPortraitAnchorOffsetY = 60;

    inline LogicalCanvas ResolveLogicalCanvas(int VW, int VH)
    {
        LogicalCanvas Canvas = {};
        if (VW <= 0 || VH <= 0)
            return Canvas;

        const float ScaleX = (float)VW / (float)FhdLogicalWidth;
        const float ScaleY = (float)VH / (float)FhdLogicalHeight;
        Canvas.Scale = std::max(0.01f, std::min(ScaleX, ScaleY));
        Canvas.Width = std::max(1, (int)std::lround((float)FhdLogicalWidth * Canvas.Scale));
        Canvas.Height = std::max(1, (int)std::lround((float)FhdLogicalHeight * Canvas.Scale));
        Canvas.X = (VW - Canvas.Width) / 2;
        Canvas.Y = VH - Canvas.Height;
        return Canvas;
    }

    inline RectI ScaleLogicalRect(const RectI& Anchor, const LogicalCanvas& Canvas)
    {
        const auto ScaleX = [&](int Value) -> int
        {
            return Canvas.X + (int)std::lround((double)Value * (double)Canvas.Scale);
        };
        const auto ScaleY = [&](int Value) -> int
        {
            return Canvas.Y + (int)std::lround((double)Value * (double)Canvas.Scale);
        };

        const int Left = ScaleX(Anchor.X);
        const int Top = ScaleY(Anchor.Y);
        const int Right = ScaleX(Anchor.X + Anchor.W);
        const int Bottom = ScaleY(Anchor.Y + Anchor.H);
        return { Left, Top, std::max(1, Right - Left), std::max(1, Bottom - Top) };
    }

    inline HudLayout BuildFhdHudLayout(int VW, int VH)
    {
        const auto Canvas = ResolveLogicalCanvas(VW, VH);
        HudLayout Layout = {};
        const int HudHeight = std::max(1, (int)std::lround((float)FhdConsoleAnchor.H * Canvas.Scale));
        Layout.ConsoleRect = { 0, VH - HudHeight, VW, HudHeight };
        const float BaseScale = (float)Layout.ConsoleRect.H /
            (float)std::max(TerranConsoleLeftSourceHeight, TerranConsoleRightSourceHeight);
        const int LeftBaseH = std::max(1, (int)std::lround((float)TerranConsoleLeftSourceHeight * BaseScale));
        const int LeftBaseX = 0;
        const int LeftBaseY = Layout.ConsoleRect.Y + Layout.ConsoleRect.H - LeftBaseH;
        const int RightBaseW = std::max(1, (int)std::lround((float)TerranConsoleRightSourceWidth * BaseScale));
        const int RightBaseH = std::max(1, (int)std::lround((float)TerranConsoleRightSourceHeight * BaseScale));
        const int RightBaseX = VW - RightBaseW;
        const int RightBaseY = Layout.ConsoleRect.Y + Layout.ConsoleRect.H - RightBaseH;

        Layout.MinimapRect =
        {
            LeftBaseX + (int)std::lround((float)TerranConsoleLeftMinimapOpening.X * BaseScale),
            LeftBaseY + (int)std::lround((float)TerranConsoleLeftMinimapOpening.Y * BaseScale),
            std::max(1, (int)std::lround((float)TerranConsoleLeftMinimapOpening.W * BaseScale)),
            std::max(1, (int)std::lround((float)TerranConsoleLeftMinimapOpening.H * BaseScale))
        };

        const int BtnW = std::max(20, (int)std::lround(46.0f * BaseScale));
        const int BtnH = std::max(12, (int)std::lround(22.0f * BaseScale));
        const int BtnGap = std::max(2, (int)std::lround(4.0f * BaseScale));
        const int BtnStartX = Layout.MinimapRect.X + std::max(2, (int)std::lround(8.0f * BaseScale));
        const int BtnY = Layout.MinimapRect.Y + std::max(2, (int)std::lround(4.0f * BaseScale));

        for (int I = 0; I < 3; ++I)
        {
            Layout.MinimapButtonRects[I] =
            {
                BtnStartX + I * (BtnW + BtnGap),
                BtnY,
                BtnW,
                BtnH
            };
        }

        const int PortraitOuterW = std::max(1, (int)std::lround((float)TerranConsolePortraitSourceWidth * BaseScale));
        const int PortraitOuterH = std::max(1, (int)std::lround((float)TerranConsolePortraitSourceHeight * BaseScale));
        const int CenterOuterW = std::max(1, (int)std::lround((float)TerranConsoleCenterSourceWidth * BaseScale));
        const int CenterOuterH = std::max(1, (int)std::lround((float)TerranConsoleCenterSourceHeight * BaseScale));
        const RectI CenterOuterRect =
        {
            Layout.ConsoleRect.X + (Layout.ConsoleRect.W - CenterOuterW) / 2,
            Layout.ConsoleRect.Y + Layout.ConsoleRect.H - CenterOuterH,
            CenterOuterW,
            CenterOuterH
        };

        Layout.InfoFrameRect = CenterOuterRect;
        Layout.InfoRect =
        {
            CenterOuterRect.X + (int)std::lround((float)TerranConsoleCenterOpening.X * BaseScale),
            CenterOuterRect.Y + (int)std::lround((float)TerranConsoleCenterOpening.Y * BaseScale),
            std::max(1, (int)std::lround((float)TerranConsoleCenterOpening.W * BaseScale)),
            std::max(1, (int)std::lround((float)TerranConsoleCenterOpening.H * BaseScale))
        };
        Layout.QueueRect = ScaleLogicalRect(FhdQueueAnchor, Canvas);
        for (std::size_t Index = 0; Index < TerranConsoleRightCommandSlots.size(); ++Index)
        {
            const auto& SourceRect = TerranConsoleRightCommandSlots[Index];
            const int Row = (int)(Index / 3u);
            const int Col = (int)(Index % 3u);
            Layout.CommandRects[Index] =
            {
                RightBaseX + (int)std::lround((float)(SourceRect.X - TerranCommandSlotOutsetX) * BaseScale) + TerranCommandColOffsetPx[(std::size_t)Col],
                RightBaseY + (int)std::lround((float)(SourceRect.Y - TerranCommandSlotOutsetY + TerranCommandSlotYOffset) * BaseScale) + TerranCommandRowOffsetPx[(std::size_t)Row],
                std::max(1, (int)std::lround((float)(SourceRect.W + TerranCommandSlotOutsetX * 2) * BaseScale) + TerranCommandWidthAdjustPx),
                std::max(1, (int)std::lround((float)(SourceRect.H + TerranCommandSlotOutsetY * 2) * BaseScale) + TerranCommandHeightAdjustPx)
            };
        }
        const RectI& PortraitAnchorSourceRect = TerranConsoleRightCommandSlots[TerranPortraitAnchorSlotIndex];
        Layout.PortraitFrameRect =
        {
            RightBaseX + (int)std::lround((float)(PortraitAnchorSourceRect.X + TerranPortraitAnchorOffsetX) * BaseScale),
            RightBaseY + (int)std::lround((float)(PortraitAnchorSourceRect.Y + TerranPortraitAnchorOffsetY) * BaseScale),
            PortraitOuterW,
            PortraitOuterH
        };
        Layout.AuxiliaryRect =
        {
            Layout.PortraitFrameRect.X + (int)std::lround((float)TerranConsolePortraitOpening.X * BaseScale),
            Layout.PortraitFrameRect.Y + (int)std::lround((float)TerranConsolePortraitOpening.Y * BaseScale),
            std::max(1, (int)std::lround((float)TerranConsolePortraitOpening.W * BaseScale)),
            std::max(1, (int)std::lround((float)TerranConsolePortraitOpening.H * BaseScale))
        };
        const int PreviewInsetX = std::max(10, (int)std::lround(12.0f * BaseScale));
        const int PreviewInsetY = std::max(10, (int)std::lround(10.0f * BaseScale));
        const int PreviewSide =
            std::clamp(std::min((int)std::lround((float)Layout.InfoRect.W * 0.18f),
                                (int)std::lround((float)Layout.InfoRect.H * 0.52f)),
                       std::max(72, (int)std::lround(78.0f * BaseScale)),
                       std::max(84, (int)std::lround(112.0f * BaseScale)));
        Layout.PreviewRect =
        {
            Layout.InfoRect.X + PreviewInsetX,
            Layout.InfoRect.Y + PreviewInsetY,
            PreviewSide,
            PreviewSide
        };
        Layout.HudTop = Layout.ConsoleRect.Y;
        Layout.HudHeight = Layout.ConsoleRect.H;
        return Layout;
    }
}

inline HudLayout ResolveHudLayout(int ViewportWidth, int ViewportHeight)
{
    return Detail::BuildFhdHudLayout(ViewportWidth, ViewportHeight);
}

inline ScvInfoLayout ResolveScvInfoLayout(const RectI& InfoRect)
{
    const int Padding = 10;
    const int PortraitSize = std::clamp(InfoRect.H - 96, 72, 108);
    const int PortraitY = InfoRect.Y + std::max(8, (InfoRect.H - PortraitSize - 44) / 2 - 10);
    const int HeaderX = InfoRect.X + Padding + PortraitSize + 14;
    const int HeaderW = std::max(120, InfoRect.W - PortraitSize - Padding * 3);
    const int FooterY = InfoRect.Y + InfoRect.H - 42;
    const int FooterH = 28;
    const int FooterGap = 8;
    const int FooterLeftW = std::max(88, (HeaderW - FooterGap) / 2);
    const int FooterRightW = std::max(88, HeaderW - FooterLeftW - FooterGap);

    ScvInfoLayout Layout = {};
    Layout.PortraitRect = { InfoRect.X + Padding, PortraitY, PortraitSize, PortraitSize };
    Layout.HeaderRect = { HeaderX, InfoRect.Y + 14, HeaderW, std::max(90, PortraitSize - 6) };
    Layout.StatIconRowRect = { HeaderX, Layout.HeaderRect.Y + 62, HeaderW, 16 };
    Layout.HpTextRect = { Layout.PortraitRect.X, Layout.PortraitRect.Y + Layout.PortraitRect.H + 8, Layout.PortraitRect.W, 14 };
    Layout.HpBarRect = { Layout.PortraitRect.X, Layout.HpTextRect.Y + 14, std::max(64, PortraitSize), 10 };
    Layout.FooterCardA = { HeaderX, FooterY, FooterLeftW, FooterH };
    Layout.FooterCardB = { HeaderX + FooterLeftW + FooterGap, FooterY, FooterRightW, FooterH };
    return Layout;
}

} // namespace Game::Client::UI

// === Consolidated from src/UI/HudMinimapGeometry.h ===
#include <algorithm>
#include <cmath>

namespace Game::Client::UI
{

inline RectI ResolveMinimapContentRect(const RectI& FrameRect, int MapWidthTiles, int MapHeightTiles)
{
    if (FrameRect.W <= 0 || FrameRect.H <= 0 || MapWidthTiles <= 0 || MapHeightTiles <= 0)
        return FrameRect;

    const float FrameAspect = (float)FrameRect.W / (float)FrameRect.H;
    const float MapAspect = (float)MapWidthTiles / (float)MapHeightTiles;

    RectI Content = FrameRect;
    if (FrameAspect > MapAspect)
    {
        Content.W = std::max(1, (int)std::lround((float)FrameRect.H * MapAspect));
        Content.X = FrameRect.X + (FrameRect.W - Content.W) / 2;
    }
    else if (FrameAspect < MapAspect)
    {
        Content.H = std::max(1, (int)std::lround((float)FrameRect.W / MapAspect));
        Content.Y = FrameRect.Y + (FrameRect.H - Content.H) / 2;
    }

    return Content;
}

inline bool MinimapScreenToWorld(const RectI& FrameRect,
                                 int MapWidthTiles,
                                 int MapHeightTiles,
                                 int ScreenX,
                                 int ScreenY,
                                 float& OutWorldX,
                                 float& OutWorldY)
{
    if (!FrameRect.Contains(ScreenX, ScreenY))
        return false;

    const RectI Content = ResolveMinimapContentRect(FrameRect, MapWidthTiles, MapHeightTiles);
    if (!Content.Contains(ScreenX, ScreenY))
        return false;

    const int ClampedX = std::clamp(ScreenX, Content.X, Content.X + std::max(0, Content.W - 1));
    const int ClampedY = std::clamp(ScreenY, Content.Y, Content.Y + std::max(0, Content.H - 1));
    const float DenomW = (float)std::max(1, Content.W - 1);
    const float DenomH = (float)std::max(1, Content.H - 1);
    const float U = (float)(ClampedX - Content.X) / DenomW;
    const float V = (float)(ClampedY - Content.Y) / DenomH;
    OutWorldX = U * (float)(MapWidthTiles * 32);
    OutWorldY = V * (float)(MapHeightTiles * 32);
    return true;
}

inline int MinimapWorldToScreenX(const RectI& ContentRect, float WorldX, float MapPixelWidth)
{
    if (ContentRect.W <= 0 || MapPixelWidth <= 0.0f)
        return ContentRect.X;

    const float U = std::clamp(WorldX / MapPixelWidth, 0.0f, 1.0f);
    return ContentRect.X + (int)std::lround(U * (float)std::max(0, ContentRect.W - 1));
}

inline int MinimapWorldToScreenY(const RectI& ContentRect, float WorldY, float MapPixelHeight)
{
    if (ContentRect.H <= 0 || MapPixelHeight <= 0.0f)
        return ContentRect.Y;

    const float V = std::clamp(WorldY / MapPixelHeight, 0.0f, 1.0f);
    return ContentRect.Y + (int)std::lround(V * (float)std::max(0, ContentRect.H - 1));
}

inline bool MinimapWorldToScreenPoint(const RectI& FrameRect,
                                      int MapWidthTiles,
                                      int MapHeightTiles,
                                      float WorldX,
                                      float WorldY,
                                      int& OutScreenX,
                                      int& OutScreenY)
{
    if (FrameRect.W <= 0 || FrameRect.H <= 0 || MapWidthTiles <= 0 || MapHeightTiles <= 0)
        return false;

    const RectI Content = ResolveMinimapContentRect(FrameRect, MapWidthTiles, MapHeightTiles);
    const float MapPxW = (float)(MapWidthTiles * 32);
    const float MapPxH = (float)(MapHeightTiles * 32);
    if (MapPxW <= 0.0f || MapPxH <= 0.0f)
        return false;

    OutScreenX = MinimapWorldToScreenX(Content, WorldX, MapPxW);
    OutScreenY = MinimapWorldToScreenY(Content, WorldY, MapPxH);
    return true;
}

} // namespace Game::Client::UI

