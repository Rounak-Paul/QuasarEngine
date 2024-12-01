#pragma once

#include <qspch.h>

namespace Quasar::Math {

// Constants
constexpr float PI = 3.14159265358979323846f;
constexpr float EPSILON = 1.192092896e-07f;
// constexpr float INFINITY = 1e30f;

// Utility functions
bool float_equal(float a, float b, float epsilon = EPSILON);

// Trigonometric functions
float sin(float radians);
float cos(float radians);
float tan(float radians);

// Square root
float sqrt(float value);

// Absolute value
float abs(float value);

// Vec2
struct alignas(8) Vec2 {
    float x = 0.0f;
    float y = 0.0f;

    Vec2() = default;
    Vec2(float x, float y);

    Vec2 operator+(const Vec2& other) const;
    Vec2 operator-(const Vec2& other) const;
    Vec2 operator*(float scalar) const;
    Vec2 operator/(float scalar) const;

    Vec2& operator+=(const Vec2& other);
    Vec2& operator-=(const Vec2& other);
    Vec2& operator*=(float scalar);
    Vec2& operator/=(float scalar);

    float length() const;
    Vec2 normalized() const;
    float dot(const Vec2& other) const;
    void print() const;
};

// Vec3
struct alignas(16) Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float padding = 0.0f; // Align to 16 bytes

    Vec3() = default;
    Vec3(float x, float y, float z);

    Vec3 operator+(const Vec3& other) const;
    Vec3 operator-(const Vec3& other) const;
    Vec3 operator*(float scalar) const;
    Vec3 operator/(float scalar) const;

    Vec3& operator+=(const Vec3& other);
    Vec3& operator-=(const Vec3& other);
    Vec3& operator*=(float scalar);
    Vec3& operator/=(float scalar);

    float length() const;
    Vec3 normalized() const;
    float dot(const Vec3& other) const;
    Vec3 cross(const Vec3& other) const;
    void print() const;
};

// Vec4
struct alignas(16) Vec4 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;

    Vec4() = default;
    Vec4(float x, float y, float z, float w);

    Vec4 operator+(const Vec4& other) const;
    Vec4 operator-(const Vec4& other) const;
    Vec4 operator*(float scalar) const;
    Vec4 operator/(float scalar) const;

    Vec4& operator+=(const Vec4& other);
    Vec4& operator-=(const Vec4& other);
    Vec4& operator*=(float scalar);
    Vec4& operator/=(float scalar);

    float length() const;
    Vec4 normalized() const;
    float dot(const Vec4& other) const;
    void print() const;
};


struct Mat4 {
    std::array<std::array<float, 4>, 4> elements;

    static Mat4 identity();
    static Mat4 translation(const Vec3& translation);
    static Mat4 scale(const Vec3& scale);
    static Mat4 rotation(float angle, const Vec3& axis);
    static Mat4 perspective(float fov, float aspect, float near, float far);
    static Mat4 orthographic(float left, float right, float bottom, float top, float near, float far);

    Mat4 operator*(const Mat4& other) const;
    Vec4 operator*(const Vec4& vec) const;
};

struct Quaternion {
    float x, y, z, w;

    Quaternion(float x = 0, float y = 0, float z = 0, float w = 1);
    static Quaternion identity();
    static Quaternion from_axis_angle(const Vec3& axis, float angle);
    Quaternion conjugate() const;
    Quaternion normalized() const;
    Quaternion operator*(const Quaternion& other) const;
    Vec3 operator*(const Vec3& vec) const;
};

struct Vertex3d {
    Vec3 position;
};

Vec3 lerp(const Vec3& start, const Vec3& end, float t);

Mat4 transform(const Vec3& position, const Quaternion& rotation, const Vec3& scale);

} // namespace Quasar::Math