#pragma once

#include <cstdint>

namespace Render
{

struct Color
{
    uint8_t R = 0;
    uint8_t G = 0;
    uint8_t B = 0;
    uint8_t A = 255;

    Color() = default;
    constexpr Color(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
        : R(r), G(g), B(b), A(a) {}

    static Color Rgb(uint8_t r, uint8_t g, uint8_t b) { return Color(r, g, b, 255); }
    static Color Rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) { return Color(r, g, b, a); }

    bool operator==(const Color& Other) const
    {
        return R == Other.R && G == Other.G && B == Other.B && A == Other.A;
    }
    bool operator!=(const Color& Other) const { return !(*this == Other); }
};

namespace Colors
{
    constexpr Color White()      { return Color(255, 255, 255, 255); }
    constexpr Color Black()      { return Color(0, 0, 0, 255); }
    constexpr Color Transparent(){ return Color(0, 0, 0, 0); }
    constexpr Color Red()        { return Color(230, 30, 30, 255); }
    constexpr Color Green()      { return Color(30, 200, 30, 255); }
    constexpr Color Blue()       { return Color(50, 100, 230, 255); }
    constexpr Color Yellow()     { return Color(240, 220, 50, 255); }
    constexpr Color Magenta()    { return Color(220, 60, 200, 255); }
    constexpr Color Cyan()       { return Color(50, 200, 200, 255); }
    constexpr Color Gray()       { return Color(120, 120, 120, 255); }
    constexpr Color DarkGray()   { return Color(40, 40, 40, 255); }
    constexpr Color LightGray()  { return Color(200, 200, 200, 255); }
    constexpr Color Orange()     { return Color(255, 160, 30, 255); }
}

} // namespace Render
