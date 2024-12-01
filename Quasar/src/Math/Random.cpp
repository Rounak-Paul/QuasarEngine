#include "Random.h"

namespace Quasar::Math {

// Random integer without range
int random_int() {
    static std::random_device rd; // Seed
    static std::mt19937 generator(rd()); // Mersenne Twister generator
    std::uniform_int_distribution<int> distribution; // Defaults to full range of int
    return distribution(generator);
}

// Random float without range (0.0 to 1.0)
float random_float() {
    static std::random_device rd; // Seed
    static std::mt19937 generator(rd()); // Mersenne Twister generator
    std::uniform_real_distribution<float> distribution(0.0f, 1.0f); // Range [0, 1)
    return distribution(generator);
}

// Random number generation
int random_int(int min, int max) {
    static std::random_device rd; // Seed
    static std::mt19937 generator(rd()); // Mersenne Twister generator
    std::uniform_int_distribution<int> distribution(min, max);
    return distribution(generator);
}

float random_float(float min, float max) {
    static std::random_device rd; // Seed
    static std::mt19937 generator(rd()); // Mersenne Twister generator
    std::uniform_real_distribution<float> distribution(min, max);
    return distribution(generator);
}

} // namespace Quasar::Math