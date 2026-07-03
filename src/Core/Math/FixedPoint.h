#pragma once

#include <cstdint>
#include <cmath>
#include <limits>
#include <type_traits>

namespace Core::Math
{

template<size_t t_integer_bits, size_t t_fractional_bits, bool t_is_signed, bool t_exact_integer_bits = false>
struct fixed_point
{
    static const bool is_signed = t_is_signed;
    static const bool exact_integer_bits = t_exact_integer_bits;
    static const size_t integer_bits = t_integer_bits;
    static const size_t fractional_bits = t_fractional_bits;
    static const size_t total_bits = integer_bits + fractional_bits;

    using raw_signed_type = std::int32_t;
    using raw_unsigned_type = std::uint32_t;
    using raw_type = typename std::conditional<is_signed, raw_signed_type, raw_unsigned_type>::type;

    raw_type raw_value = 0;

    void Wrap()
    {
        if (!exact_integer_bits) return;
        raw_value <<= (int)(32 - total_bits);
        raw_value >>= (int)(32 - total_bits);
    }

    static constexpr fixed_point FromRaw(raw_type raw_value)
    {
        fixed_point fp;
        fp.raw_value = exact_integer_bits ? (raw_type)((raw_type)(raw_value << (32 - (int)total_bits)) >> (32 - (int)total_bits)) : raw_value;
        return fp;
    }

    template<typename T, typename std::enable_if<std::is_integral<T>::value>::type* = nullptr>
    static fixed_point Integer(T integer_value)
    {
        return FromRaw((raw_type)((raw_unsigned_type)integer_value << (int)fractional_bits));
    }

    static fixed_point Zero() { return Integer(0); }
    static fixed_point One() { return Integer(1); }

    raw_type IntegerPart() const { return raw_value >> (int)fractional_bits; }
    raw_type FractionalPart() const { return raw_value & (((raw_type)1 << (int)fractional_bits) - 1); }

    int AsInt() const { return (int)IntegerPart(); }
    float AsFloat() const { return (float)raw_value / (float)(1 << (int)fractional_bits); }

    static fixed_point FromFloat(float f) { return FromRaw((raw_type)(f * (float)(1 << (int)fractional_bits))); }
    static fixed_point FromInt(int v) { return Integer(v); }

    fixed_point Floor() const { return Integer(IntegerPart()); }
    fixed_point Ceil() const { return (*this + Integer(1) - FromRaw(1)).Floor(); }
    fixed_point Abs() const { return (*this >= Zero()) ? *this : FromRaw(-raw_value); }

    auto AsSigned() const { return fixed_point<integer_bits, fractional_bits, true, exact_integer_bits>::FromRaw(raw_value); }
    auto AsUnsigned() const { return fixed_point<integer_bits, fractional_bits, false, exact_integer_bits>::FromRaw(raw_value); }

    bool operator==(const fixed_point& n) const { return raw_value == n.raw_value; }
    bool operator!=(const fixed_point& n) const { return raw_value != n.raw_value; }
    bool operator<(const fixed_point& n) const { return raw_value < n.raw_value; }
    bool operator<=(const fixed_point& n) const { return raw_value <= n.raw_value; }
    bool operator>(const fixed_point& n) const { return raw_value > n.raw_value; }
    bool operator>=(const fixed_point& n) const { return raw_value >= n.raw_value; }

    fixed_point operator-() const
    {
        static_assert(is_signed, "fixed_point: cannot negate an unsigned number");
        return FromRaw(-raw_value);
    }

    fixed_point& operator+=(const fixed_point& n) { raw_value += n.raw_value; Wrap(); return *this; }
    fixed_point operator+(const fixed_point& n) const { fixed_point r(*this); r += n; return r; }
    fixed_point& operator-=(const fixed_point& n) { raw_value -= n.raw_value; Wrap(); return *this; }
    fixed_point operator-(const fixed_point& n) const { fixed_point r(*this); r -= n; return r; }

    template<typename T, typename std::enable_if<std::is_integral<T>::value>::type* = nullptr>
    fixed_point& operator*=(T integer_value) { raw_value *= integer_value; Wrap(); return *this; }
    template<typename T, typename std::enable_if<std::is_integral<T>::value>::type* = nullptr>
    fixed_point operator*(T integer_value) const { fixed_point r(*this); r *= integer_value; return r; }
    template<typename T, typename std::enable_if<std::is_integral<T>::value>::type* = nullptr>
    fixed_point& operator/=(T integer_value) { raw_value /= integer_value; Wrap(); return *this; }
    template<typename T, typename std::enable_if<std::is_integral<T>::value>::type* = nullptr>
    fixed_point operator/(T integer_value) const { fixed_point r(*this); r /= integer_value; return r; }

    template<size_t n_integer_bits>
    fixed_point& operator*=(const fixed_point<n_integer_bits, fractional_bits, is_signed, exact_integer_bits>& n)
    {
        return *this = *this * n;
    }

    template<size_t n_integer_bits, size_t n_fractional_bits>
    auto operator*(const fixed_point<n_integer_bits, n_fractional_bits, is_signed, exact_integer_bits>& n) const
    {
        using result_type = fixed_point<(integer_bits > n_integer_bits ? integer_bits : n_integer_bits),
                                        (fractional_bits > n_fractional_bits ? fractional_bits : n_fractional_bits),
                                        is_signed, exact_integer_bits>;
        using tmp_t = std::int64_t;
        tmp_t tmp = ((tmp_t)raw_value * (tmp_t)n.raw_value) >> (int)n_fractional_bits;
        return result_type::FromRaw((typename result_type::raw_type)tmp);
    }

    template<size_t n_integer_bits, size_t n_fractional_bits>
    auto operator/(const fixed_point<n_integer_bits, n_fractional_bits, is_signed, exact_integer_bits>& n) const
    {
        using result_type = fixed_point<(integer_bits > n_integer_bits ? integer_bits : n_integer_bits),
                                        (fractional_bits > n_fractional_bits ? fractional_bits : n_fractional_bits),
                                        is_signed, exact_integer_bits>;
        using tmp_t = std::int64_t;
        tmp_t tmp = ((tmp_t)raw_value << (int)n_fractional_bits) / (tmp_t)n.raw_value;
        return result_type::FromRaw((typename result_type::raw_type)tmp);
    }
};

using fp8 = fixed_point<24, 8, true>;
using ufp8 = fixed_point<24, 8, false>;
using fp16 = fixed_point<16, 16, true>;
using direction_t = fixed_point<0, 8, true, true>;

inline fp8 operator"" _fp8(unsigned long long int value) { return fp8::FromRaw((fp8::raw_type)value); }
inline direction_t operator"" _dir(unsigned long long int value) { return direction_t::FromRaw((direction_t::raw_type)value); }

struct FixedVec2
{
    fp8 X = fp8::Zero();
    fp8 Y = fp8::Zero();

    FixedVec2() = default;
    FixedVec2(fp8 X, fp8 Y) : X(X), Y(Y) {}
    FixedVec2(float X, float Y) : X(fp8::FromFloat(X)), Y(fp8::FromFloat(Y)) {}

    FixedVec2 operator+(const FixedVec2& Other) const { return {X + Other.X, Y + Other.Y}; }
    FixedVec2 operator-(const FixedVec2& Other) const { return {X - Other.X, Y - Other.Y}; }
    FixedVec2 operator*(fp8 Scalar) const { return {X * Scalar, Y * Scalar}; }
    FixedVec2 operator/(fp8 Scalar) const { return {X / Scalar, Y / Scalar}; }
    FixedVec2 operator*(int Scalar) const { return {X * Scalar, Y * Scalar}; }
    FixedVec2 operator/(int Scalar) const { return {X / Scalar, Y / Scalar}; }

    FixedVec2& operator+=(const FixedVec2& Other) { X += Other.X; Y += Other.Y; return *this; }
    FixedVec2& operator-=(const FixedVec2& Other) { X -= Other.X; Y -= Other.Y; return *this; }
    FixedVec2& operator*=(fp8 Scalar) { X *= Scalar; Y *= Scalar; return *this; }

    fp8 LengthSq() const { return X * X + Y * Y; }
    float LengthAsFloat() const { return std::sqrt((float)X.raw_value * X.raw_value / 65536.0f + (float)Y.raw_value * Y.raw_value / 65536.0f); }

    fp8 Dot(const FixedVec2& Other) const { return X * Other.X + Y * Other.Y; }
    fp8 DistanceSqTo(const FixedVec2& Other) const { return (*this - Other).LengthSq(); }

    static FixedVec2 Zero() { return {fp8::Zero(), fp8::Zero()}; }
};

inline std::uint32_t ISqrt(std::uint32_t n)
{
    std::uint32_t r = 0;
    std::uint32_t p = (std::uint32_t)1 << 30;
    while (p > n) p >>= 2;
    while (p)
    {
        if (n >= r + p)
        {
            n -= r + p;
            r += 2 * p;
        }
        r >>= 1;
        p >>= 2;
    }
    return r;
}

} // namespace Core::Math
