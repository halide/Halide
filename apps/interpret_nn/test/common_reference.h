#ifndef COMMON_REFERENCE_H_
#define COMMON_REFERENCE_H_

#include <algorithm>
#include <cstdint>
#include <limits>

namespace interpret_nn {

// This function implements the same computation as the ARMv7 NEON VQRDMULH
// instruction.
inline int32_t multiply_2x_high(int32_t a, int32_t b) {
    int64_t a_wide = a;
    int64_t b_wide = b;
    int64_t ab_wide = a_wide * b_wide;
    int64_t nudge = 1 << 30;
    int64_t result = (ab_wide + nudge) >> 31;
    result = std::max(result, (int64_t)std::numeric_limits<int32_t>::min());
    result = std::min(result, (int64_t)std::numeric_limits<int32_t>::max());
    return (int32_t)result;
}

// Correctly-rounded-to-nearest division by a power-of-two. Also known as
// rounding arithmetic right shift.
inline int32_t round_shift_right(int32_t x, int32_t shift) {
    // Shift must satisfy 0 <= shift <= 31
    int32_t round = (1 << shift) / 2;
    return ((int64_t)x + round) >> shift;
}

// Performs right shift and multiply by a multiplier.
inline int32_t multiply_quantized(int32_t x, int32_t q, int32_t shift) {
    return round_shift_right(multiply_2x_high(x, q), shift);
}

}  // namespace interpret_nn

#endif
