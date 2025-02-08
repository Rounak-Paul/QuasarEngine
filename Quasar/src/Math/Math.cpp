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
    result.mat[0][0] = 1 - 2 * (y * y + z * z);
    result.mat[0][1] = 2 * (x * y - z * w);
    result.mat[0][2] = 2 * (x * z + y * w);
    result.mat[0][3] = 0.0f;

    result.mat[1][0] = 2 * (x * y + z * w);
    result.mat[1][1] = 1 - 2 * (x * x + z * z);
    result.mat[1][2] = 2 * (y * z - x * w);
    result.mat[1][3] = 0.0f;

    result.mat[2][0] = 2 * (x * z - y * w);
    result.mat[2][1] = 2 * (y * z + x * w);
    result.mat[2][2] = 1 - 2 * (x * x + y * y);
    result.mat[2][3] = 0.0f;

    result.mat[3][0] = 0.0f;
    result.mat[3][1] = 0.0f;
    result.mat[3][2] = 0.0f;
    result.mat[3][3] = 1.0f;

    // Apply translation to the center
    result.mat[0][3] = center.x;
    result.mat[1][3] = center.y;
    result.mat[2][3] = center.z;

    return result;
}
} // namespace Quasar::Math