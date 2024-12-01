#include "Math.h"

namespace Quasar::Math {

// Utility functions
bool float_equal(float a, float b, float epsilon) {
    return std::abs(a - b) <= epsilon;
}

// Trigonometric functions
float sin(float radians) {
    return std::sin(radians);
}

float cos(float radians) {
    return std::cos(radians);
}

float tan(float radians) {
    return std::tan(radians);
}

// Square root
float sqrt(float value) {
    assert(value >= 0.0f); // Ensure non-negative input
    return std::sqrt(value);
}

// Absolute value
float abs(float value) {
    return std::fabs(value);
}

// Vec2 implementations
Vec2::Vec2(float x, float y) : x(x), y(y) {}

Vec2 Vec2::operator+(const Vec2& other) const { return {x + other.x, y + other.y}; }
Vec2 Vec2::operator-(const Vec2& other) const { return {x - other.x, y - other.y}; }
Vec2 Vec2::operator*(float scalar) const { return {x * scalar, y * scalar}; }
Vec2 Vec2::operator/(float scalar) const {
    assert(scalar != 0.0f);
    return {x / scalar, y / scalar};
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

Vec2& Vec2::operator*=(float scalar) {
    x *= scalar;
    y *= scalar;
    return *this;
}

Vec2& Vec2::operator/=(float scalar) {
    assert(scalar != 0.0f);
    x /= scalar;
    y /= scalar;
    return *this;
}

float Vec2::length() const { return std::sqrt(x * x + y * y); }

Vec2 Vec2::normalized() const {
    float len = length();
    assert(len != 0.0f);
    return {x / len, y / len};
}

float Vec2::dot(const Vec2& other) const { return x * other.x + y * other.y; }

void Vec2::print() const { std::cout << "Vec2(" << x << ", " << y << ")\n"; }

// Vec3 implementations
Vec3::Vec3(float x, float y, float z) : x(x), y(y), z(z) {}

Vec3 Vec3::operator+(const Vec3& other) const { return {x + other.x, y + other.y, z + other.z}; }
Vec3 Vec3::operator-(const Vec3& other) const { return {x - other.x, y - other.y, z - other.z}; }
Vec3 Vec3::operator*(float scalar) const { return {x * scalar, y * scalar, z * scalar}; }
Vec3 Vec3::operator/(float scalar) const {
    assert(scalar != 0.0f);
    return {x / scalar, y / scalar, z / scalar};
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

Vec3& Vec3::operator*=(float scalar) {
    x *= scalar;
    y *= scalar;
    z *= scalar;
    return *this;
}

Vec3& Vec3::operator/=(float scalar) {
    assert(scalar != 0.0f);
    x /= scalar;
    y /= scalar;
    z /= scalar;
    return *this;
}

float Vec3::length() const { return std::sqrt(x * x + y * y + z * z); }

Vec3 Vec3::normalized() const {
    float len = length();
    assert(len != 0.0f);
    return {x / len, y / len, z / len};
}

float Vec3::dot(const Vec3& other) const { return x * other.x + y * other.y + z * other.z; }

Vec3 Vec3::cross(const Vec3& other) const {
    return {y * other.z - z * other.y, z * other.x - x * other.z, x * other.y - y * other.x};
}

void Vec3::print() const { std::cout << "Vec3(" << x << ", " << y << ", " << z << ")\n"; }

// Vec4 implementations
Vec4::Vec4(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}

Vec4 Vec4::operator+(const Vec4& other) const { return {x + other.x, y + other.y, z + other.z, w + other.w}; }
Vec4 Vec4::operator-(const Vec4& other) const { return {x - other.x, y - other.y, z - other.z, w - other.w}; }
Vec4 Vec4::operator*(float scalar) const { return {x * scalar, y * scalar, z * scalar, w * scalar}; }
Vec4 Vec4::operator/(float scalar) const {
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

Vec4& Vec4::operator*=(float scalar) {
    x *= scalar;
    y *= scalar;
    z *= scalar;
    w *= scalar;
    return *this;
}

Vec4& Vec4::operator/=(float scalar) {
    assert(scalar != 0.0f);
    x /= scalar;
    y /= scalar;
    z /= scalar;
    w /= scalar;
    return *this;
}

float Vec4::length() const { return std::sqrt(x * x + y * y + z * z + w * w); }

Vec4 Vec4::normalized() const {
    float len = length();
    assert(len != 0.0f);
    return {x / len, y / len, z / len, w / len};
}

float Vec4::dot(const Vec4& other) const { return x * other.x + y * other.y + z * other.z + w * other.w; }

void Vec4::print() const { std::cout << "Vec4(" << x << ", " << y << ", " << z << ", " << w << ")\n"; }


// Mat4
Mat4 Mat4::identity() {
    Mat4 result = {};
    for (int i = 0; i < 4; i++) result.elements[i][i] = 1.0f;
    return result;
}

Mat4 Mat4::translation(const Vec3& translation) {
    Mat4 result = Mat4::identity();
    result.elements[3][0] = translation.x;
    result.elements[3][1] = translation.y;
    result.elements[3][2] = translation.z;
    return result;
}

Mat4 Mat4::scale(const Vec3& scale) {
    Mat4 result = Mat4::identity();
    result.elements[0][0] = scale.x;
    result.elements[1][1] = scale.y;
    result.elements[2][2] = scale.z;
    return result;
}

Mat4 Mat4::rotation(float angle, const Vec3& axis) {
    Mat4 result = Mat4::identity();
    float c = cos(angle);
    float s = sin(angle);
    float omc = 1.0f - c;

    result.elements[0][0] = axis.x * axis.x * omc + c;
    result.elements[0][1] = axis.x * axis.y * omc - axis.z * s;
    result.elements[0][2] = axis.x * axis.z * omc + axis.y * s;

    result.elements[1][0] = axis.y * axis.x * omc + axis.z * s;
    result.elements[1][1] = axis.y * axis.y * omc + c;
    result.elements[1][2] = axis.y * axis.z * omc - axis.x * s;

    result.elements[2][0] = axis.z * axis.x * omc - axis.y * s;
    result.elements[2][1] = axis.z * axis.y * omc + axis.x * s;
    result.elements[2][2] = axis.z * axis.z * omc + c;

    return result;
}

Mat4 Mat4::perspective(float fov, float aspect, float near, float far) {
    Mat4 result = {};
    float tanHalfFOV = tan(fov / 2.0f);
    result.elements[0][0] = 1.0f / (aspect * tanHalfFOV);
    result.elements[1][1] = 1.0f / tanHalfFOV;
    result.elements[2][2] = -(far + near) / (far - near);
    result.elements[2][3] = -1.0f;
    result.elements[3][2] = -(2.0f * far * near) / (far - near);
    return result;
}

Mat4 Mat4::orthographic(float left, float right, float bottom, float top, float near, float far) {
    Mat4 result = Mat4::identity();
    result.elements[0][0] = 2.0f / (right - left);
    result.elements[1][1] = 2.0f / (top - bottom);
    result.elements[2][2] = -2.0f / (far - near);
    result.elements[3][0] = -(right + left) / (right - left);
    result.elements[3][1] = -(top + bottom) / (top - bottom);
    result.elements[3][2] = -(far + near) / (far - near);
    return result;
}

// Matrix-matrix multiplication.
Mat4 Mat4::operator*(const Mat4& other) const {
    Mat4 result = {};
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            result.elements[row][col] = 
                elements[row][0] * other.elements[0][col] +
                elements[row][1] * other.elements[1][col] +
                elements[row][2] * other.elements[2][col] +
                elements[row][3] * other.elements[3][col];
        }
    }
    return result;
}

// Matrix-vector multiplication.
Vec4 Mat4::operator*(const Vec4& vec) const {
    Vec4 result;
    result.x = elements[0][0] * vec.x + elements[0][1] * vec.y + elements[0][2] * vec.z + elements[0][3] * vec.w;
    result.y = elements[1][0] * vec.x + elements[1][1] * vec.y + elements[1][2] * vec.z + elements[1][3] * vec.w;
    result.z = elements[2][0] * vec.x + elements[2][1] * vec.y + elements[2][2] * vec.z + elements[2][3] * vec.w;
    result.w = elements[3][0] * vec.x + elements[3][1] * vec.y + elements[3][2] * vec.z + elements[3][3] * vec.w;
    return result;
}

Quaternion::Quaternion(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}

Quaternion Quaternion::identity() {
    return {0, 0, 0, 1};
}

Quaternion Quaternion::from_axis_angle(const Vec3& axis, float angle) {
    float halfAngle = angle / 2.0f;
    float s = sin(halfAngle);
    return {axis.x * s, axis.y * s, axis.z * s, cos(halfAngle)};
}

Quaternion Quaternion::conjugate() const {
    return {-x, -y, -z, w};
}

Quaternion Quaternion::normalized() const {
    float length = sqrt(x * x + y * y + z * z + w * w);
    return {x / length, y / length, z / length, w / length};
}

Quaternion Quaternion::operator*(const Quaternion& other) const {
    return {
        w * other.x + x * other.w + y * other.z - z * other.y,
        w * other.y - x * other.z + y * other.w + z * other.x,
        w * other.z + x * other.y - y * other.x + z * other.w,
        w * other.w - x * other.x - y * other.y - z * other.z
    };
}

Vec3 Quaternion::operator*(const Vec3& vec) const {
    Vec3 u{x, y, z};
    float s = w;
    return u * 2.0f * u.dot(vec) + vec * (s * s - u.dot(u)) + u.cross(vec) * 2.0f * s;
}

Vec3 lerp(const Vec3& start, const Vec3& end, float t) {
    return start + (end - start) * t;
}

Mat4 transform(const Vec3& position, const Quaternion& rotation, const Vec3& scale) {
    Mat4 translate = Mat4::translation(position);
    Mat4 rotate = Mat4::rotation(acos(rotation.w) * 2, (Vec3{rotation.x, rotation.y, rotation.z}).normalized());
    Mat4 scaleMat = Mat4::scale(scale);
    return translate * rotate * scaleMat;
}
} // namespace Quasar::Math