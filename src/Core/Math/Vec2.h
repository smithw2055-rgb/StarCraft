#pragma once

#include <cmath>
#include <cstdint>

namespace Core::Math
{

struct Vec2
{
    float X = 0.0f;
    float Y = 0.0f;

    Vec2() = default;
    Vec2(float X, float Y) : X(X), Y(Y) {}

    Vec2 operator+(const Vec2& Other) const { return {X + Other.X, Y + Other.Y}; }
    Vec2 operator-(const Vec2& Other) const { return {X - Other.X, Y - Other.Y}; }
    Vec2 operator*(float Scalar) const { return {X * Scalar, Y * Scalar}; }
    Vec2 operator/(float Scalar) const { return {X / Scalar, Y / Scalar}; }

    Vec2& operator+=(const Vec2& Other) { X += Other.X; Y += Other.Y; return *this; }
    Vec2& operator-=(const Vec2& Other) { X -= Other.X; Y -= Other.Y; return *this; }

    float Length() const { return std::sqrt(X * X + Y * Y); }
    float LengthSq() const { return X * X + Y * Y; }

    Vec2 Normalized() const
    {
        float Len = Length();
        if (Len > 0.0001f)
            return {X / Len, Y / Len};
        return *this;
    }

    float Dot(const Vec2& Other) const { return X * Other.X + Y * Other.Y; }
    float DistanceTo(const Vec2& Other) const { return (*this - Other).Length(); }

    static Vec2 Zero() { return {0.0f, 0.0f}; }
};

} // namespace Core::Math
