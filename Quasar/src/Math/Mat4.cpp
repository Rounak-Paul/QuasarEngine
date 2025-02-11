#include "Mat4.h"

namespace Quasar::Math {
Mat4 Mat4::identity() {
    Mat4 result = {};
    for (i32 i = 0; i < 4; i++) result.mat[i][i] = 1.0f;
    return result;
}

Mat4 Mat4::translation(const Vec3& translation) {
    Mat4 result = Mat4::identity();
    result.mat[3][0] = translation.x;
    result.mat[3][1] = translation.y;
    result.mat[3][2] = translation.z;
    return result;
}

Mat4 Mat4::scale(const Vec3& scale) {
    Mat4 result = Mat4::identity();
    result.mat[0][0] = scale.x;
    result.mat[1][1] = scale.y;
    result.mat[2][2] = scale.z;
    return result;
}

Mat4 Mat4::rotation(f32 angle, const Vec3& axis) {
    Mat4 result = Mat4::identity();
    f32 c = cos(angle);
    f32 s = sin(angle);
    f32 omc = 1.0f - c;

    result.mat[0][0] = axis.x * axis.x * omc + c;
    result.mat[0][1] = axis.x * axis.y * omc - axis.z * s;
    result.mat[0][2] = axis.x * axis.z * omc + axis.y * s;

    result.mat[1][0] = axis.y * axis.x * omc + axis.z * s;
    result.mat[1][1] = axis.y * axis.y * omc + c;
    result.mat[1][2] = axis.y * axis.z * omc - axis.x * s;

    result.mat[2][0] = axis.z * axis.x * omc - axis.y * s;
    result.mat[2][1] = axis.z * axis.y * omc + axis.x * s;
    result.mat[2][2] = axis.z * axis.z * omc + c;

    return result;
}

Mat4 Mat4::perspective(f32 fov, f32 aspect, f32 near, f32 far) {
    Mat4 result = {};
    f32 tanHalfFOV = tan(fov / 2.0f);
    result.mat[0][0] = 1.0f / (aspect * tanHalfFOV);
    result.mat[1][1] = 1.0f / tanHalfFOV;
    result.mat[2][2] = -(far + near) / (far - near);
    result.mat[2][3] = -1.0f;
    result.mat[3][2] = -(2.0f * far * near) / (far - near);
    return result;
}

Mat4 Mat4::orthographic(f32 left, f32 right, f32 bottom, f32 top, f32 near, f32 far) {
    Mat4 result = Mat4::identity();
    result.mat[0][0] = 2.0f / (right - left);
    result.mat[1][1] = 2.0f / (top - bottom);
    result.mat[2][2] = -2.0f / (far - near);
    result.mat[3][0] = -(right + left) / (right - left);
    result.mat[3][1] = -(top + bottom) / (top - bottom);
    result.mat[3][2] = -(far + near) / (far - near);
    return result;
}

Mat4 Mat4::look_at(Vec3 position, Vec3 target, Vec3 up)
{
    Vec3 z_axis = (target - position).normalized();       // Forward vector
    Vec3 x_axis = Vec3::cross(up, z_axis).normalized();          // Right vector
    Vec3 y_axis = Vec3::cross(z_axis, x_axis);                   // Up vector

    Mat4 result;

    // Rotation part
    result.mat[0] = {x_axis.x, y_axis.x, -z_axis.x, 0.0f};
    result.mat[1] = {x_axis.y, y_axis.y, -z_axis.y, 0.0f};
    result.mat[2] = {x_axis.z, y_axis.z, -z_axis.z, 0.0f};

    // Translation part
    result.mat[3] = {
        -Vec3::dot(x_axis, position),
        -Vec3::dot(y_axis, position),
        Vec3::dot(z_axis, position),
        1.0f
    };

    return result;
}

// Matrix-matrix multiplication.
Mat4 Mat4::operator*(const Mat4& other) const {
    Mat4 result = {};
    for (i32 row = 0; row < 4; ++row) {
        for (i32 col = 0; col < 4; ++col) {
            result.mat[row][col] = 
                mat[row][0] * other.mat[0][col] +
                mat[row][1] * other.mat[1][col] +
                mat[row][2] * other.mat[2][col] +
                mat[row][3] * other.mat[3][col];
        }
    }
    return result;
}

// Matrix-vector multiplication.
Vec4 Mat4::operator*(const Vec4& vec) const {
    Vec4 result;
    result.x = mat[0][0] * vec.x + mat[0][1] * vec.y + mat[0][2] * vec.z + mat[0][3] * vec.w;
    result.y = mat[1][0] * vec.x + mat[1][1] * vec.y + mat[1][2] * vec.z + mat[1][3] * vec.w;
    result.z = mat[2][0] * vec.x + mat[2][1] * vec.y + mat[2][2] * vec.z + mat[2][3] * vec.w;
    result.w = mat[3][0] * vec.x + mat[3][1] * vec.y + mat[3][2] * vec.z + mat[3][3] * vec.w;
    return result;
}

// Inverse computation
Mat4 Mat4::inverse() const {
    const auto& m = mat;
    Mat4 inv;

    // Calculate cofactors
    inv.mat[0][0] =  m[1][1] * m[2][2] * m[3][3] - m[1][1] * m[2][3] * m[3][2] - m[2][1] * m[1][2] * m[3][3] + m[2][1] * m[1][3] * m[3][2] + m[3][1] * m[1][2] * m[2][3] - m[3][1] * m[1][3] * m[2][2];
    inv.mat[0][1] = -m[0][1] * m[2][2] * m[3][3] + m[0][1] * m[2][3] * m[3][2] + m[2][1] * m[0][2] * m[3][3] - m[2][1] * m[0][3] * m[3][2] - m[3][1] * m[0][2] * m[2][3] + m[3][1] * m[0][3] * m[2][2];
    inv.mat[0][2] =  m[0][1] * m[1][2] * m[3][3] - m[0][1] * m[1][3] * m[3][2] - m[1][1] * m[0][2] * m[3][3] + m[1][1] * m[0][3] * m[3][2] + m[3][1] * m[0][2] * m[1][3] - m[3][1] * m[0][3] * m[1][2];
    inv.mat[0][3] = -m[0][1] * m[1][2] * m[2][3] + m[0][1] * m[1][3] * m[2][2] + m[1][1] * m[0][2] * m[2][3] - m[1][1] * m[0][3] * m[2][2] - m[2][1] * m[0][2] * m[1][3] + m[2][1] * m[0][3] * m[1][2];

    inv.mat[1][0] = -m[1][0] * m[2][2] * m[3][3] + m[1][0] * m[2][3] * m[3][2] + m[2][0] * m[1][2] * m[3][3] - m[2][0] * m[1][3] * m[3][2] - m[3][0] * m[1][2] * m[2][3] + m[3][0] * m[1][3] * m[2][2];
    inv.mat[1][1] =  m[0][0] * m[2][2] * m[3][3] - m[0][0] * m[2][3] * m[3][2] - m[2][0] * m[0][2] * m[3][3] + m[2][0] * m[0][3] * m[3][2] + m[3][0] * m[0][2] * m[2][3] - m[3][0] * m[0][3] * m[2][2];
    inv.mat[1][2] = -m[0][0] * m[1][2] * m[3][3] + m[0][0] * m[1][3] * m[3][2] + m[1][0] * m[0][2] * m[3][3] - m[1][0] * m[0][3] * m[3][2] - m[3][0] * m[0][2] * m[1][3] + m[3][0] * m[0][3] * m[1][2];
    inv.mat[1][3] =  m[0][0] * m[1][2] * m[2][3] - m[0][0] * m[1][3] * m[2][2] - m[1][0] * m[0][2] * m[2][3] + m[1][0] * m[0][3] * m[2][2] + m[2][0] * m[0][2] * m[1][3] - m[2][0] * m[0][3] * m[1][2];

    inv.mat[2][0] =  m[1][0] * m[2][1] * m[3][3] - m[1][0] * m[2][3] * m[3][1] - m[2][0] * m[1][1] * m[3][3] + m[2][0] * m[1][3] * m[3][1] + m[3][0] * m[1][1] * m[2][3] - m[3][0] * m[1][3] * m[2][1];
    inv.mat[2][1] = -m[0][0] * m[2][1] * m[3][3] + m[0][0] * m[2][3] * m[3][1] + m[2][0] * m[0][1] * m[3][3] - m[2][0] * m[0][3] * m[3][1] - m[3][0] * m[0][1] * m[2][3] + m[3][0] * m[0][3] * m[2][1];
    inv.mat[2][2] =  m[0][0] * m[1][1] * m[3][3] - m[0][0] * m[1][3] * m[3][1] - m[1][0] * m[0][1] * m[3][3] + m[1][0] * m[0][3] * m[3][1] + m[3][0] * m[0][1] * m[1][3] - m[3][0] * m[0][3] * m[1][1];
    inv.mat[2][3] = -m[0][0] * m[1][1] * m[2][3] + m[0][0] * m[1][3] * m[2][1] + m[1][0] * m[0][1] * m[2][3] - m[1][0] * m[0][3] * m[2][1] - m[2][0] * m[0][1] * m[1][3] + m[2][0] * m[0][3] * m[1][1];

    inv.mat[3][0] = -m[1][0] * m[2][1] * m[3][2] + m[1][0] * m[2][2] * m[3][1] + m[2][0] * m[1][1] * m[3][2] - m[2][0] * m[1][2] * m[3][1] - m[3][0] * m[1][1] * m[2][2] + m[3][0] * m[1][2] * m[2][1];
    inv.mat[3][1] =  m[0][0] * m[2][1] * m[3][2] - m[0][0] * m[2][2] * m[3][1] - m[2][0] * m[0][1] * m[3][2] + m[2][0] * m[0][2] * m[3][1] + m[3][0] * m[0][1] * m[2][2] - m[3][0] * m[0][2] * m[2][1];
    inv.mat[3][2] = -m[0][0] * m[1][1] * m[3][2] + m[0][0] * m[1][2] * m[3][1] + m[1][0] * m[0][1] * m[3][2] - m[1][0] * m[0][2] * m[3][1] - m[3][0] * m[0][1] * m[1][2] + m[3][0] * m[0][2] * m[1][1];
    inv.mat[3][3] =  m[0][0] * m[1][1] * m[2][2] - m[0][0] * m[1][2] * m[2][1] - m[1][0] * m[0][1] * m[2][2] + m[1][0] * m[0][2] * m[2][1] + m[2][0] * m[0][1] * m[1][2] - m[2][0] * m[0][2] * m[1][1];

    // Compute determinant
    float det = m[0][0] * inv.mat[0][0] + m[0][1] * inv.mat[0][1] + m[0][2] * inv.mat[0][2] + m[0][3] * inv.mat[0][3];

    if (std::abs(det) < EPSILON) {
        LOG_WARN("Matrix is not invertible.");
        return {};
    }

    // Scale by 1/det
    det = 1.0f / det;
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            inv.mat[i][j] *= det;
        }
    }

    return inv;
}

// Transpose the matrix
Mat4 Mat4::transpose() const {
    Mat4 result;
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            result.mat[i][j] = mat[j][i];
        }
    }
    return result;
}
}