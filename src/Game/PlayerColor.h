#pragma once

#include "Core/Color.h"

namespace Game
{

// CHK unit owners are zero-based player slots. Keep this mapping at render
// time so trigger ownership changes are reflected immediately rather than
// leaving a stale colour in each entity component.
inline constexpr Core::Color PlayerColorForID(int PlayerID)
{
    switch (PlayerID)
    {
        case 0: return Core::Colors::Blue();
        case 1: return Core::Colors::Red();
        case 2: return Core::Colors::Cyan();
        case 3: return Core::Colors::Magenta();
        case 4: return Core::Colors::Orange();
        case 5: return Core::Color(140, 90, 45, 255); // Brown
        case 6: return Core::Colors::White();
        case 7: return Core::Colors::Yellow();
        case 8: return Core::Colors::Green();
        case 9: return Core::Colors::LightGray();
        case 10: return Core::Color(90, 120, 230, 255); // Pale blue
        default: return Core::Colors::Gray(); // Neutral and non-player slots
    }
}

} // namespace Game
