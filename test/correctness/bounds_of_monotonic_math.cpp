#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(BoundsOfMonotonicMathTest, Basic) {
    Func f;
    Var x;

    ImageParam input(Float(32), 1);

    f(x) = input(cast<int>(ceil(0.3f * ceil(0.4f * floor(x * 22.5f)))));

    f.infer_input_bounds({10});

    Buffer<float> in = input.get();

    int correct = 26;
    EXPECT_EQ(in.width(), correct) << "Width is " << in.width() << " instead of " << correct;
}
