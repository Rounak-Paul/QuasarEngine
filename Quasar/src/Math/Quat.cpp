#include "Quat.h"

namespace Quasar::Math {
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
}