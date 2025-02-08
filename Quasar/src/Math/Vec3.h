#pragma once

#include <qspch.h>

namespace Quasar::Math {
struct alignas(16) Vec3 {
    f32 x = 0.0f;
    f32 y = 0.0f;
    f32 z = 0.0f;
    f32 padding = 0.0f; // Align to 16 bytes

    Vec3() = default;
    Vec3(f32 f);
    Vec3(f32 x, f32 y, f32 z);

    static Vec3 zero() { return Vec3{0.f}; }
    static Vec3 one() { return Vec3{1.f}; }
    static Vec3 up() { return Vec3{0.0f, 1.0f, 0.0f}; }
    static Vec3 down() { return Vec3{0.0f, -1.0f, 0.0f}; }
    static Vec3 left() { return Vec3{-1.0f, 0.0f, 0.0f}; }
    static Vec3 right() { return Vec3{1.0f, 0.0f, 0.0f}; }
    static Vec3 front() { return Vec3{0.0f, 0.0f, -1.0f}; }
    static Vec3 back() { return Vec3{0.0f, 0.0f, 1.0f}; }
    static Vec3 cross(const Vec3& v0, const Vec3& v1);
    static f32 dot(const Vec3& v0, const Vec3& v1);

    Vec3 operator+(const Vec3& other) const;
    Vec3 operator-(const Vec3& other) const;
    Vec3 operator*(f32 scalar) const;
    Vec3 operator*(const Vec3& other) const;
    Vec3 operator/(f32 scalar) const;
    Vec3 operator/(const Vec3& other) const;

    Vec3& operator+=(const Vec3& other);
    Vec3& operator-=(const Vec3& other);
    Vec3& operator*=(f32 scalar);
    Vec3& operator*=(const Vec3& other);
    Vec3& operator/=(f32 scalar);
    Vec3& operator/=(const Vec3& other);

    f32 length() const;
    Vec3 normalized() const;
    void print() const;
};
}