#include <cmath>
#include <cstdio>
#include <gtest/gtest.h>

#include "Halide.h"
using namespace Halide;

TEST(CseNanTest, NanVsOne) {
    ImageParam xyz{Float(32), 3, "xyz"};
    Target t = get_jit_target_from_environment().with_feature(Target::StrictFloat);

    Var col{"col"}, row{"row"};
    Func nan_or_one{"nan_or_one"};
    nan_or_one(col, row) = Halide::select(is_nan(xyz(col, row, 0)), NAN, 1.0f);

    Buffer<float> true_buf{1, 1, 1};
    true_buf(0, 0, 0) = NAN;

    Buffer<float> false_buf{1, 1, 1};
    false_buf(0, 0, 0) = 2.0f;

    Buffer<float> true_result{1, 1};
    Buffer<float> false_result{1, 1};

    xyz.set(true_buf);
    nan_or_one.realize({true_result}, t);

    xyz.set(false_buf);
    nan_or_one.realize({false_result}, t);
    EXPECT_TRUE(std::isnan(true_result(0, 0))) << "Expected NaN for true_result, got " << true_result(0, 0);
    EXPECT_EQ(false_result(0, 0), 1.0f) << "Expected 1.0f for false_result, got " << false_result(0, 0);
}
