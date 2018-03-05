#include "common_reference.h"

#include <algorithm>
#include <limits>

int32_t saturating_rounding_doubling_high_multiply_reference(int32_t a, int32_t b) {
    int64_t a_wide = a;
    int64_t b_wide = b;
    int64_t ab_wide = a_wide * b_wide;
    int64_t nudge = 1 << 30;
    int64_t result = (ab_wide + nudge) >> 31;
    result = std::max(result, (int64_t) std::numeric_limits<int32_t>::min());
    result = std::min(result, (int64_t) std::numeric_limits<int32_t>::max());
    return (int32_t) result;
}

int32_t rounding_shift_right_reference(int32_t x, int32_t shift) {
    // Shift must satisfy 0 <= shift <= 31
    int32_t mask = ((1ll << shift) - 1);
    int32_t remainder = x & mask;
    int32_t threshold = (mask >> 1) + (x < 0 ? 1 : 0);
    return (x >> shift) + (remainder > threshold ? 1 : 0);
}

int32_t multiply_quantized_multiplier_reference(int32_t x, int32_t q, int32_t shift) {
    return rounding_shift_right_reference(saturating_rounding_doubling_high_multiply_reference(x, q), shift);
}
