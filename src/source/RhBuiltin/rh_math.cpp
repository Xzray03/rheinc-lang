// rh_math.cpp - Runtime matematika Rheinc (RhBuiltin).

#include <cstdint>

extern "C" std::int64_t rh_pow(std::int64_t base, std::int64_t exp) {
    std::int64_t result = 1;
    for (std::int64_t i = 0; i < exp; ++i) result *= base;
    return result;
}
