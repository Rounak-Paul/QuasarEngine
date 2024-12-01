#pragma once

#include <qspch.h>

namespace Quasar::Math {
    // Random number generation
    int random_int();
    float random_float();
    int random_int(int min, int max);
    float random_float(float min, float max);
}