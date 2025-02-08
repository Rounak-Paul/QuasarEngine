#pragma once

#include <qspch.h>

namespace Quasar::Math {
struct Mat4 {
    alignas(16) std::array<f32, 4> mat[4];

    static Mat4 identity();
    static Mat4 translation(const Vec3& translation);
    static Mat4 scale(const Vec3& scale);
    static Mat4 rotation(f32 angle, const Vec3& axis);
    static Mat4 perspective(f32 fov, f32 aspect, f32 near, f32 far);
    static Mat4 orthographic(f32 left, f32 right, f32 bottom, f32 top, f32 near, f32 far);

    Mat4 mat4_look_at(Vec3 position, Vec3 target, Vec3 up);

    Mat4 operator*(const Mat4& other) const;
    Vec4 operator*(const Vec4& vec) const;

    Mat4 inverse() const;
    Mat4 transpose() const;
};
}