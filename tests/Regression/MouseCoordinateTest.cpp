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
        RequireNear(Scale.X, 0.5f, "stretched viewport maps X independently");
        RequireNear(Scale.Y, 0.6666667f, "stretched viewport maps Y independently");
        RequireNear(Scale.OffsetX, 0.0f, "stretched viewport has no X offset");
        RequireNear(Scale.OffsetY, 0.0f, "stretched viewport has no Y offset");

        const auto MidpointX = Core::Platform::MouseCoordToFramebufferChecked(
            800.0f, Scale.X, Scale.OffsetX, Scale.MaxX);
        const auto MidpointY = Core::Platform::MouseCoordToFramebufferChecked(
            450.0f, Scale.Y, Scale.OffsetY, Scale.MaxY);
        RequireNear(MidpointX.Value, 400.0f, "client midpoint maps to logical X midpoint");
        RequireNear(MidpointY.Value, 300.0f, "client midpoint maps to logical Y midpoint");
        Require(MidpointX.InsideContent && MidpointY.InsideContent,
                "full stretched viewport remains game content");
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
        RequireNear(Scale.X, 1.875f, "narrow client maps full logical X extent");
        RequireNear(Scale.Y, 1.40625f, "tall client maps full logical Y extent");
        RequireNear(Scale.OffsetX, 0.0f, "full viewport has no X offset");
        RequireNear(Scale.OffsetY, 0.0f, "full viewport has no Y offset");
        RequireNear(Core::Platform::MouseCoordToFramebuffer(0.0f, Scale.X, Scale.OffsetX, Scale.MaxX), 0.0f,
                    "client left maps to logical origin");
        RequireNear(Core::Platform::MouseCoordToFramebuffer(1023.0f, Scale.X, Scale.OffsetX, Scale.MaxX), 1918.125f,
                    "client right maps near logical right edge");
        RequireNear(Core::Platform::MouseCoordToFramebuffer(767.0f, Scale.Y, Scale.OffsetY, Scale.MaxY), 1078.59375f,
                    "client bottom maps near logical bottom edge");
    }

    {
        const auto Scale = Core::Platform::ComputeMouseScale(1001, 701, 1502, 1052, 1.5f);
        RequireNear(Core::Platform::MouseCoordToFramebuffer(
                        751.0f, Scale.X, Scale.OffsetX, Scale.MaxX),
                    500.5f,
                    "fractional DPI odd width does not accumulate horizontal drift");
        RequireNear(Core::Platform::MouseCoordToFramebuffer(
                        526.0f, Scale.Y, Scale.OffsetY, Scale.MaxY),
                    350.5f,
                    "fractional DPI odd height does not accumulate vertical drift");
    }

    std::cout << "MouseCoordinateTest passed\n";
    return 0;
}
