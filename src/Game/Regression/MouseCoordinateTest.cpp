#include "Core/Platform/MouseCoordinate.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace
{
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
        RequireNear(Scale.X, 1.0f, "high-DPI Sokol mouse X is already framebuffer");
        RequireNear(Scale.Y, 1.0f, "high-DPI Sokol mouse Y is already framebuffer");
        RequireNear(Core::Platform::MouseCoordToFramebuffer(1280.0f, Scale.X), 1280.0f,
                    "mouse X keeps framebuffer coordinates");
        RequireNear(Core::Platform::MouseCoordToFramebuffer(720.0f, Scale.Y), 720.0f,
                    "mouse Y keeps framebuffer coordinates");
    }

    {
        const auto Scale = Core::Platform::ComputeMouseScale(800, 600, 1600, 900, 1.0f);
        RequireNear(Scale.X, 1.0f, "non-square framebuffer X is not reapplied to mouse");
        RequireNear(Scale.Y, 1.0f, "non-square framebuffer Y is not reapplied to mouse");
    }

    {
        const auto Scale = Core::Platform::ComputeMouseScale(0, 0, 0, 0, 1.25f);
        RequireNear(Scale.X, 1.0f, "invalid window dimensions keep one-to-one X");
        RequireNear(Scale.Y, 1.0f, "invalid window dimensions keep one-to-one Y");
        RequireNear(Core::Platform::MouseCoordToFramebuffer(10.0f, -2.0f), 10.0f,
                    "invalid scale falls back to one-to-one coordinates");
    }

    std::cout << "MouseCoordinateTest passed\n";
    return 0;
}
