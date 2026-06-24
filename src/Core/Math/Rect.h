#pragma once

#include "Core/Math/Vec2.h"

namespace Core::Math
{

struct Rect
{
    float X = 0.0f;
    float Y = 0.0f;
    float Width = 0.0f;
    float Height = 0.0f;

    Vec2 GetPosition() const { return {X, Y}; }
    Vec2 GetCenter() const { return {X + Width * 0.5f, Y + Height * 0.5f}; }

    bool Contains(const Vec2& Point) const
    {
        return Point.X >= X && Point.X <= X + Width &&
               Point.Y >= Y && Point.Y <= Y + Height;
    }

    bool Intersects(const Rect& Other) const
    {
        return X < Other.X + Other.Width &&
               X + Width > Other.X &&
               Y < Other.Y + Other.Height &&
               Y + Height > Other.Y;
    }
};

} // namespace Core::Math
