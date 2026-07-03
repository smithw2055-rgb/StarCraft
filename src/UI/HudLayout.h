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
ScvInfoLayout ResolveScvInfoLayout(const RectI& InfoRect);

} // namespace Game::Client::UI
