#include "Halide.h"
#include <cmath>
#include <stdio.h>

using namespace Halide;

void h_assert(bool condition, const char *msg) {
    if (!condition) {
        printf("FAIL: %s\n", msg);
        abort();
    }
}

template<typename T>
bool test() {
    const T one(1.0);
    const T onePointTwoFive(1.25);

    // Check the bits are how we expect before using
    // comparision operators
    h_assert(one.to_bits() != onePointTwoFive.to_bits(), "bits should be different");
    uint16_t bits = (T::exponent_mask >> 1) & T::exponent_mask;
    h_assert(one.to_bits() == bits, "bit pattern for 1.0 is wrong");
    bits |= 1 << (T::mantissa_bits - 2);
    h_assert(onePointTwoFive.to_bits() == bits, "bit pattern for 1.25 is wrong");

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
    const T minusOne = -one;
    h_assert(minusOne < one, "-1.0 should be < 1.0");
    h_assert(one > minusOne, "1.0 should be > -1.0");

    // NaN never compares equal to itself
    const T nanValue = T::make_nan();
    h_assert(nanValue != nanValue, "NaN must not compare equal to itself");
    h_assert(!(nanValue == nanValue), "NaN must not compare equal to itself");

    // +ve zero and -ve zero are comparable
    const T zeroP = T::make_zero();
    const T zeroN = T::make_negative_zero();
    h_assert(zeroP == zeroN, "+0 and -0 should be treated as equal");

    // Infinities are comparable
    const T infinityP = T::make_infinity();
    const T infinityN = T::make_negative_infinity();
    h_assert(infinityP > infinityN, "inf+ should be > inf-");
    h_assert(infinityN < infinityP, "inf- should be < inf+");
    h_assert(one < infinityP, "1.0 should be < inf+");
    h_assert(minusOne < infinityP, "1.0 should be < inf+");
    h_assert(one > infinityN, "1.0 should be > inf-");
    h_assert(minusOne > infinityN, "-1.0 should be > inf-");

    return true;
}

int main() {
    test<float16_t>();
    test<bfloat16_t>();
    printf("Success!\n");
    return 0;
}
