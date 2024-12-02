#pragma once

#include <qspch.h>

namespace Quasar::Math {
struct alignas(16) Vec4 {
    f32 x = 0.0f;
    f32 y = 0.0f;
    f32 z = 0.0f;
    f32 w = 0.0f;

    Vec4() = default;
    Vec4(f32 f);
    Vec4(f32 x, f32 y, f32 z, f32 w);

    static Vec4 zero() { return Vec4{0.f}; }
    static Vec4 one() { return Vec4{1.f}; }
    static f32 dot(const Vec4& v0, const Vec4& v1);

    Vec4 operator+(const Vec4& other) const;
    Vec4 operator-(const Vec4& other) const;
    Vec4 operator*(f32 scalar) const;
    Vec4 operator/(f32 scalar) const;

    Vec4& operator+=(const Vec4& other);
    Vec4& operator-=(const Vec4& other);
    Vec4& operator*=(f32 scalar);
    Vec4& operator/=(f32 scalar);

    f32 length() const;
    Vec4 normalized() const;
    void print() const;
};
}