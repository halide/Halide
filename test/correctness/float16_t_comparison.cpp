#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

namespace {
template<typename T>
void test_comparisons() {
    const T one(1.0);
    const T onePointTwoFive(1.25);

    // Check the bits are how we expect before using
    // comparison operators
    EXPECT_NE(one.to_bits(), onePointTwoFive.to_bits()) << "bits should be different";
    uint16_t bits = (T::exponent_mask >> 1) & T::exponent_mask;
    EXPECT_EQ(one.to_bits(), bits) << "bit pattern for 1.0 is wrong";
    bits |= 1 << (T::mantissa_bits - 2);
    EXPECT_EQ(onePointTwoFive.to_bits(), bits) << "bit pattern for 1.25 is wrong";

    // Check comparison operators
    EXPECT_NE(one, onePointTwoFive) << "1.0 should not equal 1.25";
    EXPECT_LT(one, onePointTwoFive) << "1.0 should be less than 1.25";
    EXPECT_LE(one, onePointTwoFive) << "1.0 should be less than or equal to 1.25";
    EXPECT_GT(onePointTwoFive, one) << "1.25 should be greater than 1.0";
    EXPECT_GE(onePointTwoFive, one) << "1.25 should be greater than or equal to 1.0";
    EXPECT_GE(one, one) << "1.0 should be greater than or equal to itself";
    EXPECT_EQ(one, one) << "1.0 should equal itself";

    // Try with a negative number
    const T minusOne = -one;
    EXPECT_LT(minusOne, one) << "-1.0 should be < 1.0";
    EXPECT_GT(one, minusOne) << "1.0 should be > -1.0";

    // NaN never compares equal to itself
    const T nanValue = T::make_nan();
    EXPECT_NE(nanValue, nanValue) << "NaN must not compare equal to itself";

    // +ve zero and -ve zero are comparable
    const T zeroP = T::make_zero();
    const T zeroN = T::make_negative_zero();
    EXPECT_EQ(zeroP, zeroN) << "+0 and -0 should be treated as equal";

    // Infinities are comparable
    const T infinityP = T::make_infinity();
    const T infinityN = T::make_negative_infinity();
    EXPECT_GT(infinityP, infinityN) << "inf+ should be > inf-";
    EXPECT_LT(infinityN, infinityP) << "inf- should be < inf+";
    EXPECT_LT(one, infinityP) << "1.0 should be < inf+";
    EXPECT_LT(minusOne, infinityP) << "-1.0 should be < inf+";
    EXPECT_GT(one, infinityN) << "1.0 should be > inf-";
    EXPECT_GT(minusOne, infinityN) << "-1.0 should be > inf-";
}
}  // namespace

TEST(Float16tComparisonTest, Float16tComparison) {
    test_comparisons<float16_t>();
}

TEST(Float16tComparisonTest, Bfloat16tComparison) {
    test_comparisons<bfloat16_t>();
}
