#include "Vec2.h"

namespace Quasar::Math
{
Vec2::Vec2(f32 value) : x(value), y(value) {}

Vec2::Vec2(f32 x, f32 y) : x(x), y(y) {}

Vec2 Vec2::operator+(const Vec2& other) const { return {x + other.x, y + other.y}; }
Vec2 Vec2::operator-(const Vec2& other) const { return {x - other.x, y - other.y}; }
Vec2 Vec2::operator*(f32 scalar) const { return {x * scalar, y * scalar}; }
Vec2 Vec2::operator*(const Vec2& other) const { return x * other.x + y * other.y; }
Vec2 Vec2::operator/(f32 scalar) const {
    assert(scalar != 0.0f);
    return {x / scalar, y / scalar};
}
Vec2 Vec2::operator/(const Vec2 &other) const
{
    assert((other.x != 0.0f) && (other.y != 0.0f));
    return {x / other.x, y / other.y};
}

Vec2& Vec2::operator+=(const Vec2& other) {
    x += other.x;
    y += other.y;
    return *this;
}

Vec2& Vec2::operator-=(const Vec2& other) {
    x -= other.x;
    y -= other.y;
    return *this;
}

Vec2& Vec2::operator*=(f32 scalar) {
    x *= scalar;
    y *= scalar;
    return *this;
}

Vec2 &Vec2::operator*=(const Vec2 &other)
{
    x *= other.x;
    y *= other.y;
    return *this;
}

Vec2& Vec2::operator/=(f32 scalar) {
    assert(scalar != 0.0f);
    x /= scalar;
    y /= scalar;
    return *this;
}

Vec2 &Vec2::operator/=(const Vec2 &other)
{
    assert((other.x != 0.0f) && (other.y != 0.0f));
    x /= other.x;
    y /= other.y;
    return *this;
}

f32 Vec2::length() const { return std::sqrt(x * x + y * y); }

Vec2 Vec2::normalized() const {
    f32 len = length();
    assert(len != 0.0f);
    return {x / len, y / len};
}

void Vec2::print() const { std::cout << "Vec2(" << x << ", " << y << ")\n"; }
} // namespace Quasar::Math
