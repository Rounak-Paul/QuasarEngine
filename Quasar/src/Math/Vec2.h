#pragma once

#include <qspch.h>

namespace Quasar::Math {
struct alignas(8) Vec2 {
    f32 x = 0.0f;
    f32 y = 0.0f;

    Vec2() = default;
    Vec2(f32 f);
    Vec2(f32 x, f32 y);

    static Vec2 zero() { return Vec2{0.f}; }
    static Vec2 one() { return Vec2{1.f}; }
    static Vec2 up() { return Vec2{0.f, 1.f}; }
    static Vec2 down() { return Vec2{0.f, -1.f}; }
    static Vec2 left() { return Vec2{-1.f, 0.f}; }
    static Vec2 right() { return Vec2{1.f, 0.f}; }

    Vec2 operator+(const Vec2& other) const;
    Vec2 operator-(const Vec2& other) const;
    Vec2 operator*(f32 scalar) const;
    Vec2 operator*(const Vec2& other) const;
    Vec2 operator/(f32 scalar) const;
    Vec2 operator/(const Vec2& other) const;

    Vec2& operator+=(const Vec2& other);
    Vec2& operator-=(const Vec2& other);
    Vec2& operator*=(f32 scalar);
    Vec2& operator*=(const Vec2& other);
    Vec2& operator/=(f32 scalar);
    Vec2& operator/=(const Vec2& other);

    f32 length() const;
    Vec2 normalized() const;
    void print() const;
};
}