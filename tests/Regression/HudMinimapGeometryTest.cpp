#include "UI/HudMinimapGeometry.h"
#include "UI/HudMinimapState.h"

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
    using Game::Client::UI::MinimapWorldToScreenPoint;
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

    Game::Client::UI::HudMinimapPing Ping;
    Ping.Position = {192.0f * 32.0f * 0.25f, 128.0f * 32.0f * 0.75f};
    Ping.TimeRemaining = 3.0f;
    const int PingX = MinimapWorldToScreenX(WideContent, Ping.Position.X, 192.0f * 32.0f);
    const int PingY = MinimapWorldToScreenY(WideContent, Ping.Position.Y, 128.0f * 32.0f);
    Require(std::abs(PingX - (WideContent.X + WideContent.W / 4)) <= 1,
        "campaign minimap ping maps its world X into minimap content");
    Require(std::abs(PingY - (WideContent.Y + (WideContent.H * 3) / 4)) <= 1,
        "campaign minimap ping maps its world Y into minimap content");

    int SharedPingX = 0;
    int SharedPingY = 0;
    Require(MinimapWorldToScreenPoint(
            Frame,
            192,
            128,
            Ping.Position.X,
            Ping.Position.Y,
            SharedPingX,
            SharedPingY),
        "shared ping geometry maps campaign ping to minimap point");
    Require(std::abs(SharedPingX - PingX) <= 1,
        "shared ping geometry uses the same X mapping as explicit content mapping");
    Require(std::abs(SharedPingY - PingY) <= 1,
        "shared ping geometry uses the same Y mapping as explicit content mapping");
    Require(!MinimapWorldToScreenPoint(Frame, 0, 128, Ping.Position.X, Ping.Position.Y, SharedPingX, SharedPingY),
        "shared ping geometry rejects invalid map dimensions");

    // Verify HudMinimapDot dimensions
    Game::Client::UI::HudMinimapDot DefaultDot;
    Require(DefaultDot.Width == 2 && DefaultDot.Height == 2,
        "default minimap dot is 2x2 pixels");
    Game::Client::UI::HudMinimapDot BuildingDot;
    BuildingDot.Width = 4;
    BuildingDot.Height = 4;
    Require(BuildingDot.Width == 4 && BuildingDot.Height == 4,
        "building minimap dot supports custom footprint");

    // Verify Camera Viewport Rect mapping to Minimap screen space
    Game::Client::UI::HudMinimapState MinimapState;
    MinimapState.CamVisibleX = 192.0f * 32.0f * 0.25f;
    MinimapState.CamVisibleY = 128.0f * 32.0f * 0.25f;
    MinimapState.CamVisibleW = 640.0f;
    MinimapState.CamVisibleH = 480.0f;
    const int BoxX0 = MinimapWorldToScreenX(WideContent, MinimapState.CamVisibleX, 192.0f * 32.0f);
    const int BoxY0 = MinimapWorldToScreenY(WideContent, MinimapState.CamVisibleY, 128.0f * 32.0f);
    const int BoxX1 = MinimapWorldToScreenX(WideContent, MinimapState.CamVisibleX + MinimapState.CamVisibleW, 192.0f * 32.0f);
    const int BoxY1 = MinimapWorldToScreenY(WideContent, MinimapState.CamVisibleY + MinimapState.CamVisibleH, 128.0f * 32.0f);
    Require(BoxX1 > BoxX0, "camera viewport box has positive width on minimap");
    Require(BoxY1 > BoxY0, "camera viewport box has positive height on minimap");
    Require(BoxX0 >= WideContent.X && BoxX1 <= WideContent.X + WideContent.W,
        "camera viewport box stays within minimap content horizontal bounds");

    std::printf("HudMinimapGeometryTest passed\n");
    return 0;
}
