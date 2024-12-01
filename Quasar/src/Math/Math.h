#pragma once

#include <qspch.h>

namespace Quasar::Math {

// Constants
constexpr f32 PI = 3.14159265358979323846f;
constexpr f32 EPSILON = 1.192092896e-07f;
// constexpr f32 INFINITY = 1e30f;

// Utility functions
b8 f32_equal(f32 a, f32 b, f32 epsilon = EPSILON);

// Trigonometric functions
f32 sin(f32 radians);
f32 cos(f32 radians);
f32 tan(f32 radians);

// Square root
f32 sqrt(f32 value);

// Absolute value
f32 abs(f32 value);

// Vec2
struct alignas(8) Vec2 {
    f32 x = 0.0f;
    f32 y = 0.0f;

    Vec2() = default;
    Vec2(f32 x, f32 y);

    Vec2 operator+(const Vec2& other) const;
    Vec2 operator-(const Vec2& other) const;
    Vec2 operator*(f32 scalar) const;
    Vec2 operator/(f32 scalar) const;

    Vec2& operator+=(const Vec2& other);
    Vec2& operator-=(const Vec2& other);
    Vec2& operator*=(f32 scalar);
    Vec2& operator/=(f32 scalar);

    f32 length() const;
    Vec2 normalized() const;
    f32 dot(const Vec2& other) const;
    void print() const;
};

// Vec3
struct alignas(16) Vec3 {
    f32 x = 0.0f;
    f32 y = 0.0f;
    f32 z = 0.0f;
    f32 padding = 0.0f; // Align to 16 bytes

    Vec3() = default;
    Vec3(f32 x, f32 y, f32 z);

    Vec3 operator+(const Vec3& other) const;
    Vec3 operator-(const Vec3& other) const;
    Vec3 operator*(f32 scalar) const;
    Vec3 operator/(f32 scalar) const;

    Vec3& operator+=(const Vec3& other);
    Vec3& operator-=(const Vec3& other);
    Vec3& operator*=(f32 scalar);
    Vec3& operator/=(f32 scalar);

    f32 length() const;
    Vec3 normalized() const;
    f32 dot(const Vec3& other) const;
    Vec3 cross(const Vec3& other) const;
    void print() const;
};

// Vec4
struct alignas(16) Vec4 {
    f32 x = 0.0f;
    f32 y = 0.0f;
    f32 z = 0.0f;
    f32 w = 0.0f;

    Vec4() = default;
    Vec4(f32 x, f32 y, f32 z, f32 w);

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
    f32 dot(const Vec4& other) const;
    void print() const;
};


struct Mat4 {
    std::array<std::array<f32, 4>, 4> elements;

    static Mat4 identity();
    static Mat4 translation(const Vec3& translation);
    static Mat4 scale(const Vec3& scale);
    static Mat4 rotation(f32 angle, const Vec3& axis);
    static Mat4 perspective(f32 fov, f32 aspect, f32 near, f32 far);
    static Mat4 orthographic(f32 left, f32 right, f32 bottom, f32 top, f32 near, f32 far);

    Mat4 operator*(const Mat4& other) const;
    Vec4 operator*(const Vec4& vec) const;
};

struct Quaternion {
    f32 x, y, z, w;

    Quaternion(f32 x = 0, f32 y = 0, f32 z = 0, f32 w = 1);
    static Quaternion identity();
    static Quaternion from_axis_angle(const Vec3& axis, f32 angle);
    Quaternion conjugate() const;
    Quaternion normalized() const;
    Quaternion operator*(const Quaternion& other) const;
    Vec3 operator*(const Vec3& vec) const;
};

struct Vertex3d {
    Vec3 position;
};

Vec3 lerp(const Vec3& start, const Vec3& end, f32 t);
Mat4 transform(const Vec3& position, const Quaternion& rotation, const Vec3& scale);
f32 deg_to_rad(f32 degrees);

} // namespace Quasar::Math