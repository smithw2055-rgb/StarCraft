#include "Core/Platform/MouseCoordinate.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace
{
    void Require(bool Condition, const char* Message)
    {
        if (!Condition)
        {
            std::cerr << "Requirement failed: " << Message << "\n";
            std::exit(1);
        }
    }

    void RequireNear(float Actual, float Expected, const char* Message)
    {
        if (std::fabs(Actual - Expected) > 0.001f)
        {
            std::cerr << "Requirement failed: " << Message
                      << " actual=" << Actual << " expected=" << Expected << "\n";
            std::exit(1);
        }
    }
}

int main()
{
    {
        const auto Scale = Core::Platform::ComputeMouseScale(1280, 720, 2560, 1440, 1.0f);
        RequireNear(Scale.X, 0.5f, "high-DPI mouse X remaps to logical pixels");
        RequireNear(Scale.Y, 0.5f, "high-DPI mouse Y remaps to logical pixels");

        const auto RightEdge = Core::Platform::MouseCoordToFramebufferChecked(
            2559.0f, Scale.X, Scale.OffsetX, Scale.MaxX);
        const auto BottomEdge = Core::Platform::MouseCoordToFramebufferChecked(
            1439.0f, Scale.Y, Scale.OffsetY, Scale.MaxY);
        RequireNear(RightEdge.Value, 1279.0f, "mouse X reaches logical right edge");
        RequireNear(BottomEdge.Value, 719.0f, "mouse Y reaches logical bottom edge");
        Require(RightEdge.InsideContent, "right edge remains inside content");
        Require(BottomEdge.InsideContent, "bottom edge remains inside content");
    }

    {
        const auto Scale = Core::Platform::ComputeMouseScale(800, 600, 1600, 900, 1.0f);
        RequireNear(Scale.X, 0.6666667f, "pillarbox X maps back to logical scale");
        RequireNear(Scale.Y, 0.6666667f, "pillarbox Y maps back to logical scale");
        RequireNear(Scale.OffsetX, 200.0f, "pillarbox offset is centered on X");
        RequireNear(Scale.OffsetY, 0.0f, "pillarbox has no Y offset");

        const auto LeftPillar = Core::Platform::MouseCoordToFramebufferChecked(
            100.0f, Scale.X, Scale.OffsetX, Scale.MaxX);
        const auto ContentLeft = Core::Platform::MouseCoordToFramebufferChecked(
            200.0f, Scale.X, Scale.OffsetX, Scale.MaxX);
        const auto ContentRight = Core::Platform::MouseCoordToFramebufferChecked(
            1399.0f, Scale.X, Scale.OffsetX, Scale.MaxX);
        const auto RightPillar = Core::Platform::MouseCoordToFramebufferChecked(
            1400.0f, Scale.X, Scale.OffsetX, Scale.MaxX);

        RequireNear(LeftPillar.Value, 0.0f, "left pillar clamps to content edge");
        Require(!LeftPillar.InsideContent, "left pillar is not game content");
        RequireNear(ContentLeft.Value, 0.0f, "content left maps to zero");
        Require(ContentLeft.InsideContent, "content left is game content");
        RequireNear(ContentRight.Value, 799.0f, "content right clamps to logical max");
        Require(ContentRight.InsideContent, "content right is game content");
        RequireNear(RightPillar.Value, 799.0f, "right pillar clamps to content edge");
        Require(!RightPillar.InsideContent, "right pillar is not game content");
    }

    {
        const auto Scale = Core::Platform::ComputeMouseScale(0, 0, 0, 0, 1.25f);
        RequireNear(Scale.X, 1.0f, "invalid window dimensions keep one-to-one X");
        RequireNear(Scale.Y, 1.0f, "invalid window dimensions keep one-to-one Y");
        RequireNear(Core::Platform::MouseCoordToFramebuffer(10.0f, -2.0f, 0.0f, 0.0f), 10.0f,
                    "invalid scale falls back to one-to-one coordinates");
        Require(Core::Platform::MouseCoordToFramebufferChecked(
                    10.0f, -2.0f, 0.0f, 0.0f).InsideContent,
                "invalid max keeps checked mapping permissive");
    }

    {
        const auto Scale = Core::Platform::ComputeMouseScale(1920, 1080, 1024, 768, 1.0f);
        RequireNear(Scale.X, 1.875f, "letterbox X maps back to logical scale");
        RequireNear(Scale.Y, 1.875f, "letterbox Y maps back to logical scale");
        RequireNear(Scale.OffsetX, 0.0f, "no X offset is applied during ratio remap");
        RequireNear(Scale.OffsetY, 96.0f, "letterbox offset is centered on Y");
        RequireNear(Core::Platform::MouseCoordToFramebuffer(0.0f, Scale.X, Scale.OffsetX, Scale.MaxX), 0.0f,
                    "left content edge maps to logical origin");
        RequireNear(Core::Platform::MouseCoordToFramebuffer(1024.0f, Scale.X, Scale.OffsetX, Scale.MaxX), 1919.0f,
                    "right content edge clamps to logical max");

        const auto TopLetterbox = Core::Platform::MouseCoordToFramebufferChecked(
            95.0f, Scale.Y, Scale.OffsetY, Scale.MaxY);
        const auto ContentTop = Core::Platform::MouseCoordToFramebufferChecked(
            96.0f, Scale.Y, Scale.OffsetY, Scale.MaxY);
        const auto ContentBottom = Core::Platform::MouseCoordToFramebufferChecked(
            671.0f, Scale.Y, Scale.OffsetY, Scale.MaxY);
        const auto BottomLetterbox = Core::Platform::MouseCoordToFramebufferChecked(
            673.0f, Scale.Y, Scale.OffsetY, Scale.MaxY);

        RequireNear(TopLetterbox.Value, 0.0f, "top letterbox clamps to content edge");
        Require(!TopLetterbox.InsideContent, "top letterbox is not game content");
        RequireNear(ContentTop.Value, 0.0f, "content top maps to zero");
        Require(ContentTop.InsideContent, "content top is game content");
        RequireNear(ContentBottom.Value, 1078.125f, "content bottom maps near logical max");
        Require(ContentBottom.InsideContent, "content bottom is game content");
        RequireNear(BottomLetterbox.Value, 1079.0f, "bottom letterbox clamps to content edge");
        Require(!BottomLetterbox.InsideContent, "bottom letterbox is not game content");
    }

    std::cout << "MouseCoordinateTest passed\n";
    return 0;
}
