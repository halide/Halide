#include "common_reference.h"

#include <algorithm>
#include <limits>

namespace interpret_nn {

int32_t SaturatingRoundingDoublingHighMultiplyReference(int32_t a, int32_t b) {
    int64_t a_wide = a;
    int64_t b_wide = b;
    int64_t ab_wide = a_wide * b_wide;
    int64_t nudge = 1 << 30;
    int64_t result = (ab_wide + nudge) >> 31;
    result = std::max(result, (int64_t)std::numeric_limits<int32_t>::min());
    result = std::min(result, (int64_t)std::numeric_limits<int32_t>::max());
    return (int32_t)result;
}

int32_t RoundingDivideByPOTReference(int32_t x, int32_t shift) {
    // Shift must satisfy 0 <= shift <= 31
    int32_t round = 1 << (shift - 1);
    return ((int64_t)x + round) >> shift;
}

int32_t MultiplyByQuantizedMultiplierSmallerThanOneReference(int32_t x, int32_t q, int32_t shift) {
    return RoundingDivideByPOTReference(SaturatingRoundingDoublingHighMultiplyReference(x, q), shift);
}

}  // namespace interpret_nn

