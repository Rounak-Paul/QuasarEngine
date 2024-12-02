#pragma once

#include <qspch.h>

#include "MathTypes.h"
#include "Vec2.h"
#include "Vec3.h"
#include "Vec4.h"

namespace Quasar::Math {

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


struct Mat4 {
    alignas(16) std::array<f32, 4> mat[4];

    static Mat4 identity();
    static Mat4 translation(const Vec3& translation);
    static Mat4 scale(const Vec3& scale);
    static Mat4 rotation(f32 angle, const Vec3& axis);
    static Mat4 perspective(f32 fov, f32 aspect, f32 near, f32 far);
    static Mat4 orthographic(f32 left, f32 right, f32 bottom, f32 top, f32 near, f32 far);

    Mat4 operator*(const Mat4& other) const;
    Vec4 operator*(const Vec4& vec) const;

    Mat4 inverse() const;
    Mat4 transpose() const;
};

struct Quat {
    f32 x, y, z, w;

    Quat(f32 x = 0, f32 y = 0, f32 z = 0, f32 w = 1);
    static Quat identity();
    static Quat from_axis_angle(const Vec3& axis, f32 angle);
    Quat conjugate() const;
    Quat normalized() const;
    Quat operator*(const Quat& other) const;
    Vec3 operator*(const Vec3& vec) const;
};

struct Vertex3d {
    Vec3 position;
};

Vec3 lerp(const Vec3& start, const Vec3& end, f32 t);
Mat4 transform(const Vec3& position, const Quat& rotation, const Vec3& scale);
f32 deg_to_rad(f32 degrees);
Mat4 quat_to_rotation_matrix(Quat q, Vec3 center);

} // namespace Quasar::Math