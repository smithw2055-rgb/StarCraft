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
        RequireNear(Scale.X, 2.0f, "high-DPI mouse X scale maps window to framebuffer");
        RequireNear(Scale.Y, 2.0f, "high-DPI mouse Y scale maps window to framebuffer");
        RequireNear(Core::Platform::MouseCoordToFramebuffer(640.0f, Scale.X), 1280.0f,
                    "mouse X converts to framebuffer coordinates");
        RequireNear(Core::Platform::MouseCoordToFramebuffer(360.0f, Scale.Y), 720.0f,
                    "mouse Y converts to framebuffer coordinates");
    }

    {
        const auto Scale = Core::Platform::ComputeMouseScale(800, 600, 1600, 900, 1.0f);
        RequireNear(Scale.X, 2.0f, "non-square framebuffer X scale is preserved");
        RequireNear(Scale.Y, 1.5f, "non-square framebuffer Y scale is preserved");
    }

    {
        const auto Scale = Core::Platform::ComputeMouseScale(0, 0, 0, 0, 1.25f);
        RequireNear(Scale.X, 1.25f, "invalid window dimensions use DPI fallback X");
        RequireNear(Scale.Y, 1.25f, "invalid window dimensions use DPI fallback Y");
        RequireNear(Core::Platform::MouseCoordToFramebuffer(10.0f, -2.0f), 10.0f,
                    "invalid scale falls back to one-to-one coordinates");
    }

    std::cout << "MouseCoordinateTest passed\n";
    return 0;
}
