#pragma once

#include <algorithm>
#include <cmath>

#include "UI/HudLayout.h"

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
