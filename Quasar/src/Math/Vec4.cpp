#include "Vec4.h"

namespace Quasar::Math
{
Vec4::Vec4(f32 f) : x(f), y(f), z(f), w(f) {}

Vec4::Vec4(f32 x, f32 y, f32 z, f32 w) : x(x), y(y), z(z), w(w) {}

f32 Vec4::dot(const Vec4 &v0, const Vec4 &v1)
{
    return v0.x * v1.x + v0.y * v1.y + v0.z * v1.z + v0.w * v1.w;
}

Vec4 Vec4::operator+(const Vec4 &other) const { return {x + other.x, y + other.y, z + other.z, w + other.w}; }
Vec4 Vec4::operator-(const Vec4& other) const { return {x - other.x, y - other.y, z - other.z, w - other.w}; }
Vec4 Vec4::operator*(f32 scalar) const { return {x * scalar, y * scalar, z * scalar, w * scalar}; }
Vec4 Vec4::operator/(f32 scalar) const {
    assert(scalar != 0.0f);
    return {x / scalar, y / scalar, z / scalar, w / scalar};
}

Vec4& Vec4::operator+=(const Vec4& other) {
    x += other.x;
    y += other.y;
    z += other.z;
    w += other.w;
    return *this;
}

Vec4& Vec4::operator-=(const Vec4& other) {
    x -= other.x;
    y -= other.y;
    z -= other.z;
    w -= other.w;
    return *this;
}

Vec4& Vec4::operator*=(f32 scalar) {
    x *= scalar;
    y *= scalar;
    z *= scalar;
    w *= scalar;
    return *this;
}

Vec4& Vec4::operator/=(f32 scalar) {
    assert(scalar != 0.0f);
    x /= scalar;
    y /= scalar;
    z /= scalar;
    w /= scalar;
    return *this;
}

f32 Vec4::length() const { return std::sqrt(x * x + y * y + z * z + w * w); }

Vec4 Vec4::normalized() const {
    f32 len = length();
    assert(len != 0.0f);
    return {x / len, y / len, z / len, w / len};
}

void Vec4::print() const { std::cout << "Vec4(" << x << ", " << y << ", " << z << ", " << w << ")\n"; }
}