#include "Vec3.h"

namespace Quasar::Math
{
Vec3::Vec3(f32 f) : x(f), y(f), z(f) {}

Vec3::Vec3(f32 x, f32 y, f32 z) : x(x), y(y), z(z) {}

Vec3 Vec3::operator+(const Vec3& other) const { return {x + other.x, y + other.y, z + other.z}; }
Vec3 Vec3::operator-(const Vec3& other) const { return {x - other.x, y - other.y, z - other.z}; }
Vec3 Vec3::operator*(f32 scalar) const { return {x * scalar, y * scalar, z * scalar}; }
Vec3 Vec3::operator*(const Vec3 &other) const { return {x * other.x, y * other.y, z * other.z}; }
Vec3 Vec3::operator/(f32 scalar) const
{
    assert(scalar != 0.0f);
    return {x / scalar, y / scalar, z / scalar};
}

Vec3 Vec3::operator/(const Vec3 &other) const
{
    assert((other.x != 0.0f) && (other.y != 0.0f) && (other.z != 0.0f));
    return {x / other.x, y / other.y, z / other.z};
}

Vec3& Vec3::operator+=(const Vec3& other) {
    x += other.x;
    y += other.y;
    z += other.z;
    return *this;
}

Vec3& Vec3::operator-=(const Vec3& other) {
    x -= other.x;
    y -= other.y;
    z -= other.z;
    return *this;
}

Vec3& Vec3::operator*=(f32 scalar) {
    x *= scalar;
    y *= scalar;
    z *= scalar;
    return *this;
}

Vec3 &Vec3::operator*=(const Vec3 &other)
{
    x *= other.x;
    y *= other.y;
    z *= other.z;
    return *this;
}

Vec3& Vec3::operator/=(f32 scalar) {
    assert(scalar != 0.0f);
    x /= scalar;
    y /= scalar;
    z /= scalar;
    return *this;
}

Vec3 &Vec3::operator/=(const Vec3 &other)
{
    assert((other.x != 0.0f) && (other.y != 0.0f) && (other.z != 0.0f));
    x /= other.x;
    y /= other.y;
    z /= other.z;
    return *this;
}

f32 Vec3::length() const { return std::sqrt(x * x + y * y + z * z); }

Vec3 Vec3::normalized() const {
    f32 len = length();
    assert(len != 0.0f);
    return {x / len, y / len, z / len};
}

Vec3 Vec3::cross(const Vec3& v0, const Vec3& v1) {
    return {v0.y * v1.z - v0.z * v1.y, v0.z * v1.x - v0.x * v1.z, v0.x * v1.y - v0.y * v1.x};
}

f32 Vec3::dot(const Vec3 &v0, const Vec3 &v1)
{
    f32 p = 0;
    p += v0.x * v1.x;
    p += v0.y * v1.y;
    p += v0.z * v1.z;
    return p;
}

void Vec3::print() const { std::cout << "Vec3(" << x << ", " << y << ", " << z << ")\n"; }

}