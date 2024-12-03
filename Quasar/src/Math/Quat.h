#pragma once

#include <qspch.h>

namespace Quasar::Math {
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
}