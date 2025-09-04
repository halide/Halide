#include "Halide.h"
#include <gtest/gtest.h>
#include <limits>

#ifdef _MSC_VER
#pragma warning(disable : 4800)  // forcing value to bool 'true' or 'false'
#endif

using namespace Halide;

namespace {
Var zero_val, one_val, weight;

template<typename weight_t>
double weight_type_scale() {
    if (std::numeric_limits<weight_t>::is_integer) {
        return std::numeric_limits<weight_t>::max();
    }
    return static_cast<weight_t>(1.0);
}

template<typename value_t>
double conversion_rounding() {
    if (std::numeric_limits<value_t>::is_integer) {
        return 0.5;
    }
    return 0.0;
}

template<typename value_t>
value_t convert_to_value(double interpolated) {
    return static_cast<value_t>(interpolated);
}

template<>
bool convert_to_value<bool>(double interpolated) {
    return interpolated >= 1;  // Already has rounding added in
}

// TODO: Use the RelativelyEqual matcher from math.cpp
template<typename value_t>
bool relatively_equal(value_t a, value_t b) {
    if (a == b) {
        return true;
    } else if (!std::numeric_limits<value_t>::is_integer) {
        // This test seems a bit high.
        double da = (double)a, db = (double)b;
        if (fabs(db - da) < .0001) {
            return true;
        }

        double relative_error;
        if (fabs(da) > fabs(db)) {
            relative_error = fabs((db - da) / da);
        } else {
            relative_error = fabs((db - da) / db);
        }

        if (relative_error < .0000002) {
            return true;
        }
        std::cerr << "relatively_equal failed for (" << a << ", " << b << ") "
                  << "with relative error " << relative_error << "\n";
    }
    return false;
}

template<typename value_t, typename weight_t>
void check_range(int32_t zero_min, int32_t zero_extent, value_t zero_offset, value_t zero_scale,
                 int32_t one_min, int32_t one_extent, value_t one_offset, value_t one_scale,
                 int32_t weight_min, int32_t weight_extent, weight_t weight_offset, weight_t weight_scale) {
    // Stuff everything in Params as these can represent uint32_t where
    // that fails in converting to Expr is we just use the raw C++ variables.
    Param<value_t> zero_scale_p, zero_offset_p;
    zero_scale_p.set(zero_scale);
    zero_offset_p.set(zero_offset);
    Param<value_t> one_scale_p, one_offset_p;
    one_scale_p.set(one_scale);
    one_offset_p.set(one_offset);
    Param<weight_t> weight_scale_p, weight_offset_p;
    weight_scale_p.set(weight_scale);
    weight_offset_p.set(weight_offset);

    Func lerp_test("lerp_test");
    lerp_test(zero_val, one_val, weight) =
        lerp(cast<value_t>((zero_val + zero_min) * zero_scale_p + zero_offset_p),
             cast<value_t>((one_val + one_min) * one_scale_p + one_offset_p),
             cast<weight_t>((weight + weight_min) * weight_scale_p + weight_offset_p));

    Buffer<value_t> result(zero_extent, one_extent, weight_extent);
    lerp_test.realize(result);

    for (int32_t i = 0; i < result.extent(0); i++) {
        for (int32_t j = 0; j < result.extent(1); j++) {
            for (int32_t k = 0; k < result.extent(2); k++) {
                value_t zero_verify = ((i + zero_min) * zero_scale + zero_offset);
                value_t one_verify = ((j + one_min) * one_scale + one_offset);
                weight_t weight_verify = (weight_t)((k + weight_min) * weight_scale + weight_offset);
                double actual_weight = weight_verify / weight_type_scale<weight_t>();

                double verify_val_full = zero_verify * (1.0 - actual_weight) + one_verify * actual_weight;
                if (verify_val_full < 0) {
                    verify_val_full -= conversion_rounding<value_t>();
                } else {
                    verify_val_full += conversion_rounding<value_t>();
                }

                value_t verify_val = convert_to_value<value_t>(verify_val_full);
                value_t computed_val = result(i, j, k);

                EXPECT_TRUE(relatively_equal(verify_val, computed_val))
                    << "Expected " << verify_val
                    << " got " << computed_val
                    << " for lerp(" << zero_verify
                    << ", " << one_verify
                    << ", " << weight_verify
                    << ") " << actual_weight << ". ";
            }
        }
    }
}
}  // namespace

TEST(LerpTest, BoolUint8Exhaustive) {
    check_range<bool, uint8_t>(0, 2, 0, 1,
                               0, 2, 0, 1,
                               0, 256, 0, 1);
}

TEST(LerpTest, Uint8Uint8Exhaustive) {
    check_range<uint8_t, uint8_t>(0, 256, 0, 1,
                                  0, 256, 0, 1,
                                  0, 256, 0, 1);
}

TEST(LerpTest, Int8Uint8Exhaustive) {
    check_range<int8_t, uint8_t>(0, 256, -128, 1,
                                 0, 256, -128, 1,
                                 0, 256, 0, 1);
}

TEST(LerpTest, Uint8FloatExhaustive) {
    check_range<uint8_t, float>(0, 256, 0, 1,
                                0, 256, 0, 1,
                                0, 256, 0, 1 / 255.0f);
}

TEST(LerpTest, Int8FloatExhaustive) {
    check_range<int8_t, float>(0, 256, -128, 1,
                               0, 256, -128, 1,
                               0, 256, 0, 1 / 255.0f);
}

TEST(LerpTest, Uint16AllZeroStarts) {
    check_range<uint16_t, uint16_t>(0, 65536, 0, 1,
                                    65535, 1, 0, 1,
                                    0, 257, 255, 1);
}

TEST(LerpTest, Uint16AllOneStarts) {
    check_range<uint16_t, uint16_t>(65535, 1, 0, 1,
                                    0, 65536, 0, 1,
                                    0, 257, 255, 1);
}

TEST(LerpTest, Uint16Uint8WeightTest) {
    check_range<uint16_t, uint8_t>(0, 1, 0, 1,
                                   65535, 1, 0, 1,
                                   0, 255, 1, 1);
}

TEST(LerpTest, Uint16Uint32WeightTest) {
    check_range<uint16_t, uint32_t>(0, 1, 0, 1,
                                    65535, 1, 0, 1,
                                    std::numeric_limits<int32_t>::min(), 257, 255 * 65535, 1);
}

TEST(LerpTest, Uint32Uint8WeightTest) {
    check_range<uint32_t, uint8_t>(0, 1, 0, 1,
                                   0x80000000, 1, 0, 1,
                                   0, 255, 0, 1);
}

TEST(LerpTest, Uint32Uint16WeightTest) {
    check_range<uint32_t, uint16_t>(0, 1, 0, 1,
                                    0x80000000, 1, 0, 1,
                                    0, 65535, 0, 1);
}

TEST(LerpTest, Uint16FloatWeightTest) {
    check_range<uint16_t, float>(0, 1, 0, 1,
                                 65535, 1, 0, 1,
                                 0, 257, 0, 255.0f / 65535.0f);
}

TEST(LerpTest, Int16AllZeroStarts) {
    check_range<int16_t, uint16_t>(0, 65536, -32768, 1,
                                   0, 1, 0, 1,
                                   0, 257, 0, 255);
}

// takes too long, difficult to test with uint32_t
// Check all delta values for 32-bit, do it in signed arithmetic
TEST(LerpTest, DISABLED_Unt32AllZeroStarts) {
    check_range<int32_t, uint32_t>(std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max(), 0, 1,
                                   0x80000000, 1, 0, 1,
                                   0, 1, 0x80000000, 1);
}

TEST(LerpTest, FloatFloatSmallRange) {
    check_range<float, float>(0, 100, 0, .01f,
                              0, 100, 0, .01f,
                              0, 100, 0, .01f);
}

TEST(LerpTest, FloatFloatNegativeRange) {
    check_range<float, float>(0, 100, -5, .1f,
                              0, 100, 0, .1f,
                              0, 100, 0, .1f);
}

TEST(LerpTest, FloatUint8WeightTest) {
    check_range<float, uint8_t>(0, 100, -5, .1f,
                                0, 100, 0, .1f,
                                0, 255, 0, 1);
}

TEST(LerpTest, FloatUint16WeightTest) {
    check_range<float, uint16_t>(0, 100, -5, .1f,
                                 0, 100, 0, .1f,
                                 0, 255, 0, 257);
}

TEST(LerpTest, FloatUint32WeightTest) {
    check_range<float, uint32_t>(0, 100, -5, .1f,
                                 0, 100, 0, .1f,
                                 std::numeric_limits<int32_t>::min(), 257, 255 * 65535, 1);
}

TEST(LerpTest, ConstantCase) {
    Func lerp_constants("lerp_constants");
    lerp_constants() = lerp(0, cast<uint32_t>(1023), .5f);
    Buffer<uint32_t> result = lerp_constants.realize();

    uint32_t expected = evaluate<uint32_t>(cast<uint32_t>(lerp(0, cast<uint16_t>(1023), .5f)));
    EXPECT_EQ(result(0), expected);
}

TEST(LerpTest, Uint32CoverageWithCasts) {
    Buffer<uint8_t> input_a_img(16, 16);
    Buffer<uint8_t> input_b_img(16, 16);

    for (int i = 0; i < 16; i++) {
        for (int j = 0; j < 16; j++) {
            input_a_img(i, j) = (i << 4) + j;
            input_b_img(i, j) = ((15 - i) << 4) + (15 - j);
        }
    }

    ImageParam input_a(UInt(8), 2);
    ImageParam input_b(UInt(8), 2);

    Var x, y;
    Func lerp_with_casts;
    Param<float> w;
    lerp_with_casts(x, y) = lerp(cast<int32_t>(input_a(x, y)), cast<int32_t>(input_b(x, y)), w);
    lerp_with_casts.vectorize(x, 4);

    input_a.set(input_a_img);
    input_b.set(input_b_img);

    w.set(0.0f);
    Buffer<int32_t> result_should_be_a = lerp_with_casts.realize({16, 16});
    w.set(1.0f);
    Buffer<int32_t> result_should_be_b = lerp_with_casts.realize({16, 16});

    for (int i = 0; i < 16; i++) {
        for (int j = 0; j < 16; j++) {
            EXPECT_EQ(input_a_img(i, j), result_should_be_a(i, j));
            EXPECT_EQ(input_b_img(i, j), result_should_be_b(i, j));
        }
    }
}
