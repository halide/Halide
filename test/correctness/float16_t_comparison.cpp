#include "Halide.h"
#include <stdio.h>
#include <cmath>

using namespace Halide;

void h_assert(bool condition, const char* msg) {
    if (!condition) {
        printf("FAIL: %s\n", msg);
        abort();
    }
}

int main() {
    const float16_t one(1.0);
    const float16_t onePointTwoFive(1.25);

    // Check the bits are how we expect before using
    // comparision operators
    h_assert(one.to_bits() != onePointTwoFive.to_bits(), "bits should be different");
    h_assert(one.to_bits() == 0x3c00, "bit pattern for 1.0 is wrong");
    h_assert(onePointTwoFive.to_bits() == 0x3d00, "bit pattern for 1.25 is wrong");

    // Check comparision operators
    h_assert(!(one == onePointTwoFive), "comparision failed");
    h_assert(one != onePointTwoFive, "comparision failed");
    h_assert(one < onePointTwoFive, "comparision failed");
    h_assert(one <= onePointTwoFive, "comparision failed");
    h_assert(onePointTwoFive > one, "comparision failed");
    h_assert(onePointTwoFive >= one, "comparision failed");
    h_assert(one >= one, "comparision failed");
    h_assert(one == one, "comparision failed");

    // Try with a negative number
    const float16_t minusOne = -one;
    h_assert(minusOne < one, "-1.0 should be < 1.0");
    h_assert(one > minusOne, "1.0 should be > -1.0");

    // NaN never compares equal to itself
    const float16_t nanValue = float16_t::make_nan();
    h_assert(nanValue != nanValue, "NaN must not compare equal to itself");
    h_assert(!(nanValue == nanValue), "NaN must not compare equal to itself");

    // +ve zero and -ve zero are comparable
    const float16_t zeroP = float16_t::make_zero(/*positive=*/true);
    const float16_t zeroN = float16_t::make_zero(/*positive=*/false);
    h_assert(zeroP == zeroN, "+0 and -0 should be treated as equal");

    // Infinities are comparable
    const float16_t infinityP = float16_t::make_infinity(/*positive=*/true);
    const float16_t infinityN = float16_t::make_infinity(/*positive=*/false);
    h_assert(infinityP > infinityN, "inf+ should be > inf-");
    h_assert(infinityN < infinityP, "inf- should be < inf+");
    h_assert(one < infinityP, "1.0 should be < inf+");
    h_assert(minusOne < infinityP, "1.0 should be < inf+");
    h_assert(one > infinityN, "1.0 should be > inf-");
    h_assert(minusOne > infinityN, "-1.0 should be > inf-");

    printf("Success!\n");
    return 0;
}
