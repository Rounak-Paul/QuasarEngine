#pragma once

#include <qspch.h>

#include "MathTypes.h"
#include "Vec2.h"
#include "Vec3.h"
#include "Vec4.h"
#include "Mat4.h"
#include "Quat.h"

namespace Quasar::Math {

// TODO: temp
struct UniformBufferObject {
    alignas(16) Mat4 model;
    alignas(16) Mat4 view;
    alignas(16) Mat4 proj;
};

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

struct Vertex {
    Vec2 pos;
    Vec3 color;
};

Vec3 lerp(const Vec3& start, const Vec3& end, f32 t);
Mat4 transform(const Vec3& position, const Quat& rotation, const Vec3& scale);
f32 deg_to_rad(f32 degrees);
Mat4 quat_to_rotation_matrix(Quat q, Vec3 center);

} // namespace Quasar::Math