#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(StrictFloatBoundsTest, Basic) {
    Target t = get_jit_target_from_environment().with_feature(Target::StrictFloat);

    Var x;
    ImageParam input(Float(32), 1);
    Param<float> f_param;

    Buffer<float> input_buffer(1);
    input_buffer.fill(2.5f);

    Func output;
    output(x) = input(x + cast<int>(f_param));

    input.set(input_buffer);
    f_param.set(0.0f);
    // This test verifies that this realize() doesn't explode in bounds infererence
    // with "unbounded access of input"
    Buffer<float> result = output.realize({1}, t);
    EXPECT_EQ(result(0), 2.5f);
}
