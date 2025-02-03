#pragma once
#include <qspch.h>

namespace Quasar::Math
{
// Constants
constexpr f32 PI = 3.14159265358979323846f;
constexpr f32 EPSILON = 1.192092896e-07f;
// constexpr f32 INFINITY = 1e30f;

typedef struct extent {
    u32 width;
    u32 height;
    u32 depth;
} extent;

} // namespace Quasar::Math
