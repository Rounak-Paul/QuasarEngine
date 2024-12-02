#include "Math.h"

namespace Quasar::Math {

// Utility functions
b8 f32_equal(f32 a, f32 b, f32 epsilon) {
    return std::abs(a - b) <= epsilon;
}

// Trigonometric functions
f32 sin(f32 radians) {
    return std::sin(radians);
}

f32 cos(f32 radians) {
    return std::cos(radians);
}

f32 tan(f32 radians) {
    return std::tan(radians);
}

// Square root
f32 sqrt(f32 value) {
    assert(value >= 0.0f); // Ensure non-negative input
    return std::sqrt(value);
}

// Absolute value
f32 abs(f32 value) {
    return std::fabs(value);
}
// Vec4 implementations
Vec4::Vec4(f32 x, f32 y, f32 z, f32 w) : x(x), y(y), z(z), w(w) {}

Vec4 Vec4::operator+(const Vec4& other) const { return {x + other.x, y + other.y, z + other.z, w + other.w}; }
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

f32 Vec4::dot(const Vec4& other) const { return x * other.x + y * other.y + z * other.z + w * other.w; }

void Vec4::print() const { std::cout << "Vec4(" << x << ", " << y << ", " << z << ", " << w << ")\n"; }


// Mat4
Mat4 Mat4::identity() {
    Mat4 result = {};
    for (i32 i = 0; i < 4; i++) result.elements[i][i] = 1.0f;
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

Mat4 Mat4::rotation(f32 angle, const Vec3& axis) {
    Mat4 result = Mat4::identity();
    f32 c = cos(angle);
    f32 s = sin(angle);
    f32 omc = 1.0f - c;

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

Mat4 Mat4::perspective(f32 fov, f32 aspect, f32 near, f32 far) {
    Mat4 result = {};
    f32 tanHalfFOV = tan(fov / 2.0f);
    result.elements[0][0] = 1.0f / (aspect * tanHalfFOV);
    result.elements[1][1] = 1.0f / tanHalfFOV;
    result.elements[2][2] = -(far + near) / (far - near);
    result.elements[2][3] = -1.0f;
    result.elements[3][2] = -(2.0f * far * near) / (far - near);
    return result;
}

Mat4 Mat4::orthographic(f32 left, f32 right, f32 bottom, f32 top, f32 near, f32 far) {
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
    for (i32 row = 0; row < 4; ++row) {
        for (i32 col = 0; col < 4; ++col) {
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

// Inverse computation
Mat4 Mat4::inverse() const {
    const auto& m = elements;
    Mat4 inv;

    // Calculate cofactors
    inv.elements[0][0] =  m[1][1] * m[2][2] * m[3][3] - m[1][1] * m[2][3] * m[3][2] - m[2][1] * m[1][2] * m[3][3] + m[2][1] * m[1][3] * m[3][2] + m[3][1] * m[1][2] * m[2][3] - m[3][1] * m[1][3] * m[2][2];
    inv.elements[0][1] = -m[0][1] * m[2][2] * m[3][3] + m[0][1] * m[2][3] * m[3][2] + m[2][1] * m[0][2] * m[3][3] - m[2][1] * m[0][3] * m[3][2] - m[3][1] * m[0][2] * m[2][3] + m[3][1] * m[0][3] * m[2][2];
    inv.elements[0][2] =  m[0][1] * m[1][2] * m[3][3] - m[0][1] * m[1][3] * m[3][2] - m[1][1] * m[0][2] * m[3][3] + m[1][1] * m[0][3] * m[3][2] + m[3][1] * m[0][2] * m[1][3] - m[3][1] * m[0][3] * m[1][2];
    inv.elements[0][3] = -m[0][1] * m[1][2] * m[2][3] + m[0][1] * m[1][3] * m[2][2] + m[1][1] * m[0][2] * m[2][3] - m[1][1] * m[0][3] * m[2][2] - m[2][1] * m[0][2] * m[1][3] + m[2][1] * m[0][3] * m[1][2];

    inv.elements[1][0] = -m[1][0] * m[2][2] * m[3][3] + m[1][0] * m[2][3] * m[3][2] + m[2][0] * m[1][2] * m[3][3] - m[2][0] * m[1][3] * m[3][2] - m[3][0] * m[1][2] * m[2][3] + m[3][0] * m[1][3] * m[2][2];
    inv.elements[1][1] =  m[0][0] * m[2][2] * m[3][3] - m[0][0] * m[2][3] * m[3][2] - m[2][0] * m[0][2] * m[3][3] + m[2][0] * m[0][3] * m[3][2] + m[3][0] * m[0][2] * m[2][3] - m[3][0] * m[0][3] * m[2][2];
    inv.elements[1][2] = -m[0][0] * m[1][2] * m[3][3] + m[0][0] * m[1][3] * m[3][2] + m[1][0] * m[0][2] * m[3][3] - m[1][0] * m[0][3] * m[3][2] - m[3][0] * m[0][2] * m[1][3] + m[3][0] * m[0][3] * m[1][2];
    inv.elements[1][3] =  m[0][0] * m[1][2] * m[2][3] - m[0][0] * m[1][3] * m[2][2] - m[1][0] * m[0][2] * m[2][3] + m[1][0] * m[0][3] * m[2][2] + m[2][0] * m[0][2] * m[1][3] - m[2][0] * m[0][3] * m[1][2];

    inv.elements[2][0] =  m[1][0] * m[2][1] * m[3][3] - m[1][0] * m[2][3] * m[3][1] - m[2][0] * m[1][1] * m[3][3] + m[2][0] * m[1][3] * m[3][1] + m[3][0] * m[1][1] * m[2][3] - m[3][0] * m[1][3] * m[2][1];
    inv.elements[2][1] = -m[0][0] * m[2][1] * m[3][3] + m[0][0] * m[2][3] * m[3][1] + m[2][0] * m[0][1] * m[3][3] - m[2][0] * m[0][3] * m[3][1] - m[3][0] * m[0][1] * m[2][3] + m[3][0] * m[0][3] * m[2][1];
    inv.elements[2][2] =  m[0][0] * m[1][1] * m[3][3] - m[0][0] * m[1][3] * m[3][1] - m[1][0] * m[0][1] * m[3][3] + m[1][0] * m[0][3] * m[3][1] + m[3][0] * m[0][1] * m[1][3] - m[3][0] * m[0][3] * m[1][1];
    inv.elements[2][3] = -m[0][0] * m[1][1] * m[2][3] + m[0][0] * m[1][3] * m[2][1] + m[1][0] * m[0][1] * m[2][3] - m[1][0] * m[0][3] * m[2][1] - m[2][0] * m[0][1] * m[1][3] + m[2][0] * m[0][3] * m[1][1];

    inv.elements[3][0] = -m[1][0] * m[2][1] * m[3][2] + m[1][0] * m[2][2] * m[3][1] + m[2][0] * m[1][1] * m[3][2] - m[2][0] * m[1][2] * m[3][1] - m[3][0] * m[1][1] * m[2][2] + m[3][0] * m[1][2] * m[2][1];
    inv.elements[3][1] =  m[0][0] * m[2][1] * m[3][2] - m[0][0] * m[2][2] * m[3][1] - m[2][0] * m[0][1] * m[3][2] + m[2][0] * m[0][2] * m[3][1] + m[3][0] * m[0][1] * m[2][2] - m[3][0] * m[0][2] * m[2][1];
    inv.elements[3][2] = -m[0][0] * m[1][1] * m[3][2] + m[0][0] * m[1][2] * m[3][1] + m[1][0] * m[0][1] * m[3][2] - m[1][0] * m[0][2] * m[3][1] - m[3][0] * m[0][1] * m[1][2] + m[3][0] * m[0][2] * m[1][1];
    inv.elements[3][3] =  m[0][0] * m[1][1] * m[2][2] - m[0][0] * m[1][2] * m[2][1] - m[1][0] * m[0][1] * m[2][2] + m[1][0] * m[0][2] * m[2][1] + m[2][0] * m[0][1] * m[1][2] - m[2][0] * m[0][2] * m[1][1];

    // Compute determinant
    float det = m[0][0] * inv.elements[0][0] + m[0][1] * inv.elements[0][1] + m[0][2] * inv.elements[0][2] + m[0][3] * inv.elements[0][3];

    if (std::abs(det) < EPSILON) {
        LOG_WARN("Matrix is not invertible.");
        return {};
    }

    // Scale by 1/det
    det = 1.0f / det;
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            inv.elements[i][j] *= det;
        }
    }

    return inv;
}

// Transpose the matrix
Mat4 Mat4::transpose() const {
    Mat4 result;
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            result.elements[i][j] = elements[j][i];
        }
    }
    return result;
}

Quat::Quat(f32 x, f32 y, f32 z, f32 w) : x(x), y(y), z(z), w(w) {}

Quat Quat::identity() {
    return {0, 0, 0, 1};
}

Quat Quat::from_axis_angle(const Vec3& axis, f32 angle) {
    f32 halfAngle = angle / 2.0f;
    f32 s = sin(halfAngle);
    return {axis.x * s, axis.y * s, axis.z * s, cos(halfAngle)};
}

Quat Quat::conjugate() const {
    return {-x, -y, -z, w};
}

Quat Quat::normalized() const {
    f32 length = sqrt(x * x + y * y + z * z + w * w);
    return {x / length, y / length, z / length, w / length};
}

Quat Quat::operator*(const Quat& other) const {
    return {
        w * other.x + x * other.w + y * other.z - z * other.y,
        w * other.y - x * other.z + y * other.w + z * other.x,
        w * other.z + x * other.y - y * other.x + z * other.w,
        w * other.w - x * other.x - y * other.y - z * other.z
    };
}

Vec3 Quat::operator*(const Vec3& vec) const {
    Vec3 u{x, y, z};
    f32 s = w;
    return u * 2.0f * Math::Vec3::dot(u, vec) + vec * (s * s - Math::Vec3::dot(u, u)) + Math::Vec3::cross(u, vec) * 2.0f * s;
}

Vec3 lerp(const Vec3& start, const Vec3& end, f32 t) {
    return start + (end - start) * t;
}

Mat4 transform(const Vec3& position, const Quat& rotation, const Vec3& scale) {
    Mat4 translate = Mat4::translation(position);
    Mat4 rotate = Mat4::rotation(acos(rotation.w) * 2, (Vec3{rotation.x, rotation.y, rotation.z}).normalized());
    Mat4 scaleMat = Mat4::scale(scale);
    return translate * rotate * scaleMat;
}

f32 deg_to_rad(f32 degrees) {
    return degrees * (PI / 180.0f);
}
Mat4 quat_to_rotation_matrix(Quat q, Vec3 center)
{
    Mat4 result;

    // Normalize the quaternion
    float magnitude = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    float x = q.x / magnitude;
    float y = q.y / magnitude;
    float z = q.z / magnitude;
    float w = q.w / magnitude;

    // Compute rotation matrix from quaternion
    result.elements[0][0] = 1 - 2 * (y * y + z * z);
    result.elements[0][1] = 2 * (x * y - z * w);
    result.elements[0][2] = 2 * (x * z + y * w);
    result.elements[0][3] = 0.0f;

    result.elements[1][0] = 2 * (x * y + z * w);
    result.elements[1][1] = 1 - 2 * (x * x + z * z);
    result.elements[1][2] = 2 * (y * z - x * w);
    result.elements[1][3] = 0.0f;

    result.elements[2][0] = 2 * (x * z - y * w);
    result.elements[2][1] = 2 * (y * z + x * w);
    result.elements[2][2] = 1 - 2 * (x * x + y * y);
    result.elements[2][3] = 0.0f;

    result.elements[3][0] = 0.0f;
    result.elements[3][1] = 0.0f;
    result.elements[3][2] = 0.0f;
    result.elements[3][3] = 1.0f;

    // Apply translation to the center
    result.elements[0][3] = center.x;
    result.elements[1][3] = center.y;
    result.elements[2][3] = center.z;

    return result;
}
} // namespace Quasar::Math