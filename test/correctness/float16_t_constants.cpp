#include "Halide.h"
#include <cmath>
#include <gtest/gtest.h>

using namespace Halide;

TEST(Float16Constants, PositiveZero) {
    // Try constructing positive zero in different ways and check they all represent
    // the same float16_t
    const float16_t zeroDefaultConstructor;
    const float16_t zeroP = float16_t::make_zero();
    const float16_t zeroPFromFloat(0.0f);
    const float16_t zeroPFromDouble(0.0);
    const float16_t zeroPFromInt(0);
    EXPECT_EQ(zeroDefaultConstructor.to_bits(), zeroP.to_bits()) << "Mismatch between constructors";
    EXPECT_EQ(zeroPFromFloat.to_bits(), zeroP.to_bits()) << "Mismatch between constructors";
    EXPECT_EQ(zeroPFromDouble.to_bits(), zeroP.to_bits()) << "Mismatch between constructors";
    EXPECT_EQ(zeroPFromInt.to_bits(), zeroP.to_bits()) << "make_from_signed_int gave wrong value";

    // Check the representation
    EXPECT_TRUE(zeroP.is_zero() && !zeroP.is_negative()) << "positive zero invalid";
    EXPECT_EQ(zeroP.to_bits(), 0x0000) << "positive zero invalid bits";

    // Try converting to native float types
    EXPECT_EQ((float)zeroP, 0.0f) << "positive zero conversion to float invalid";
    EXPECT_EQ((double)zeroP, 0.0) << "positive zero conversion to double invalid";
}

TEST(Float16Constants, NegativeZero) {
    // Try constructing negative zero in different ways and check they all represent
    // the same float16_t
    const float16_t zeroN = float16_t::make_negative_zero();
    const float16_t zeroNFromFloat(-0.0f);
    const float16_t zeroNFromDouble(-0.0);
    EXPECT_EQ(zeroNFromFloat.to_bits(), zeroN.to_bits()) << "Mismatch between constructors";
    EXPECT_EQ(zeroNFromDouble.to_bits(), zeroN.to_bits()) << "Mismatch between constructors";

    // Check the representation
    EXPECT_EQ(zeroN.to_bits(), 0x8000) << "negative zero invalid bits";
    EXPECT_TRUE(zeroN.is_zero()) << "negative zero is not zero";
    EXPECT_TRUE(zeroN.is_negative()) << "negative zero is not negative";

    // Try converting to native float types
    EXPECT_EQ((float)zeroN, -0.0f) << "negative zero conversion to float invalid";
    EXPECT_EQ((double)zeroN, -0.0) << "negative zero conversion to double invalid";
}

TEST(Float16Constants, PositiveInfinity) {
    // Try constructing positive infinity in different ways and check they all
    // represent the same float16_t
    const float16_t infinityP = float16_t::make_infinity();
    const float16_t infinityPFromFloat(std::numeric_limits<float>::infinity());
    const float16_t infinityPFromDouble(std::numeric_limits<double>::infinity());
    EXPECT_EQ(infinityPFromFloat.to_bits(), infinityP.to_bits()) << "Mismatch between constructors";
    EXPECT_EQ(infinityPFromDouble.to_bits(), infinityP.to_bits()) << "Mismatch between constructors";

    // Check the representation
    EXPECT_TRUE(infinityP.is_infinity() && !infinityP.is_negative()) << "positive infinity invalid";
    EXPECT_EQ(infinityP.to_bits(), 0x7c00) << "positive infinity invalid bits";

    // Try converting to native float types
    float infinityPf = (float)infinityP;
    double infinityPd = (double)infinityP;
    EXPECT_TRUE(std::isinf(infinityPf) && !std::signbit(infinityPf)) << "positive infinity conversion to float invalid";
    EXPECT_TRUE(std::isinf(infinityPd) && !std::signbit(infinityPd)) << "positive infinity conversion to double invalid";
}

TEST(Float16Constants, NegativeInfinity) {
    // Try constructing negative infinity in different ways and check they all
    // represent the same float16_t
    const float16_t infinityN = float16_t::make_negative_infinity();
    const float16_t infinityNFromFloat(-std::numeric_limits<float>::infinity());
    const float16_t infinityNFromDouble(-std::numeric_limits<double>::infinity());
    EXPECT_EQ(infinityNFromFloat.to_bits(), infinityN.to_bits()) << "Mismatch between constructors";
    EXPECT_EQ(infinityNFromDouble.to_bits(), infinityN.to_bits()) << "Mismatch between constructors";

    // Check the representation
    EXPECT_TRUE(infinityN.is_infinity() && infinityN.is_negative()) << "negative infinity invalid";
    EXPECT_EQ(infinityN.to_bits(), 0xfc00) << "negative infinity invalid bits";

    // Try converting to native float types
    float infinityNf = (float)infinityN;
    double infinityNd = (double)infinityN;
    EXPECT_TRUE(std::isinf(infinityNf) && std::signbit(infinityNf)) << "negative infinity conversion to float invalid";
    EXPECT_TRUE(std::isinf(infinityNd) && std::signbit(infinityNd)) << "negative infinity conversion to double invalid";
}

TEST(Float16Constants, NaN) {
    // Try constructing NaN in different ways and check they all
    // represent the same float16_t
    const float16_t nanValue = float16_t::make_nan();
    const float16_t nanValueFromFloat(std::numeric_limits<float>::quiet_NaN());
    const float16_t nanValueFromDouble(std::numeric_limits<double>::quiet_NaN());
    EXPECT_EQ(nanValueFromFloat.to_bits(), nanValue.to_bits()) << "Mismatch between constructors";
    EXPECT_EQ(nanValueFromDouble.to_bits(), nanValue.to_bits()) << "Mismatch between constructors";

    // Check the representation
    EXPECT_TRUE(nanValue.is_nan()) << "NaN invalid";
    // Check exponent is all ones
    EXPECT_EQ((nanValue.to_bits() & 0x7c00), 0x7c00) << "NaN exponent invalid";
    // Check significand is non zero
    EXPECT_GT((nanValue.to_bits() & 0x03ff), 0) << "NaN significant invalid";

    // Try converting to native float types
    float nanValuef = (float)nanValue;
    double nanValued = (double)nanValue;
    EXPECT_TRUE(std::isnan(nanValuef)) << "NaN conversion to float invalid";
    EXPECT_TRUE(std::isnan(nanValued)) << "NaN conversion to double invalid";
}

TEST(Float16Constants, Rounding) {
    auto test_rounding = [](double val, uint16_t bits) {
        const float16_t val_f16(val);
        EXPECT_EQ(val_f16.to_bits(), bits) << "Error rounding " << val << " -> " << val_f16;
    };
    test_rounding(1.0 / (1 << 24), 0x001);    // smallest positive
    test_rounding(-1.0 / (1 << 24), 0x8001);  // smallest negative
    test_rounding(65504, 0x7bff);             // largest positive
    test_rounding(-65504, 0xfbff);            // largest negative
    test_rounding(0.1, 0x2e66);
    test_rounding(0.3, 0x34cd);
    test_rounding(4091, 0x6bfe);
    test_rounding(-4091, 0xebfe);
    test_rounding(1000000, 0x7c00);   // Out-of-range maps to +infinity
    test_rounding(-1000000, 0xfc00);  // Out-of-range maps to -infinity
}
