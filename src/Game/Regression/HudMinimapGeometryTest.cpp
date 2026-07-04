#include "UI/HudMinimapGeometry.h"

#include <cstdio>
#include <cstdlib>

namespace
{
    void Require(bool Value, const char* Message)
    {
        if (!Value)
        {
            std::fprintf(stderr, "FAIL: %s\n", Message);
            std::exit(1);
        }
    }

    void RequireNear(float Actual, float Expected, float Epsilon, const char* Message)
    {
        const float Diff = Actual > Expected ? Actual - Expected : Expected - Actual;
        if (Diff > Epsilon)
        {
            std::fprintf(stderr, "FAIL: %s (actual %.3f expected %.3f)\n",
                Message, Actual, Expected);
            std::exit(1);
        }
    }
}

int main()
{
    using Game::Client::UI::MinimapScreenToWorld;
    using Game::Client::UI::MinimapWorldToScreenX;
    using Game::Client::UI::MinimapWorldToScreenY;
    using Game::Client::UI::RectI;
    using Game::Client::UI::ResolveMinimapContentRect;

    const RectI Frame{100, 200, 236, 236};
    const RectI WideContent = ResolveMinimapContentRect(Frame, 192, 128);

    Require(WideContent.X == 100, "wide maps keep full minimap width");
    Require(WideContent.W == 236, "wide maps use the full horizontal minimap span");
    Require(WideContent.H == 157, "wide maps are letterboxed to preserve aspect");
    Require(WideContent.Y > Frame.Y, "wide maps center vertically inside the minimap frame");

    float WX = 0.0f;
    float WY = 0.0f;
    Require(!MinimapScreenToWorld(Frame, 192, 128, Frame.X + 8, Frame.Y + 8, WX, WY),
        "clicks in letterbox padding do not move the camera");

    Require(MinimapScreenToWorld(
            Frame,
            192,
            128,
            WideContent.X + WideContent.W / 2,
            WideContent.Y + WideContent.H / 2,
            WX,
            WY),
        "clicks inside minimap content map to world coordinates");
    RequireNear(WX, 192.0f * 32.0f * 0.5f, 20.0f,
        "minimap center maps near the map center on X");
    RequireNear(WY, 128.0f * 32.0f * 0.5f, 20.0f,
        "minimap center maps near the map center on Y");
    const int RoundTripX = MinimapWorldToScreenX(WideContent, WX, 192.0f * 32.0f);
    const int RoundTripY = MinimapWorldToScreenY(WideContent, WY, 128.0f * 32.0f);
    Require(std::abs(RoundTripX - (WideContent.X + WideContent.W / 2)) <= 1,
        "minimap click X maps back to the displayed proportional content point");
    Require(std::abs(RoundTripY - (WideContent.Y + WideContent.H / 2)) <= 1,
        "minimap click Y maps back to the displayed proportional content point");

    const int PX = MinimapWorldToScreenX(WideContent, 192.0f * 32.0f, 192.0f * 32.0f);
    const int PY = MinimapWorldToScreenY(WideContent, 128.0f * 32.0f, 128.0f * 32.0f);
    Require(PX == WideContent.X + WideContent.W - 1,
        "world-to-minimap clamps right edge inside content rect");
    Require(PY == WideContent.Y + WideContent.H - 1,
        "world-to-minimap clamps bottom edge inside content rect");

    std::printf("HudMinimapGeometryTest passed\n");
    return 0;
}
