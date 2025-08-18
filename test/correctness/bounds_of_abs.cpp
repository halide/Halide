#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

namespace {

ImageParam input(Int(32), 1);
Var x;

void check(Func f, ImageParam in, int min, int extent) {
    Buffer<int> output(12345);
    output.set_min(-1234);

    in.reset();
    f.infer_input_bounds(output);
    Buffer<int> im = in.get();

    EXPECT_EQ(im.min(0), min);
    EXPECT_EQ(im.extent(0), extent);
}
}  // namespace

TEST(BoundsTest, AbsCastInt8) {
    // input should be required from 0 to 128 inclusive, because abs
    // of an int 8 can return 128. This is an extent of 129.
    Func f1 = lambda(x, input(abs(cast<int8_t>(x))));
    check(f1, input, 0, 129);
}

TEST(BoundsTest, AbsCastInt16) {
    Func func = lambda(x, input(abs(cast<int16_t>(x))));
    check(func, input, 0, 32769);
}

TEST(BoundsTest, AbsCastFloatToInt) {
    // cast from int to float is treated as lossless, so we get 12345 - 1234
    Func func = lambda(x, input(cast<int32_t>(abs(cast<float>(x)))));
    check(func, input, 0, 11111);
}

TEST(BoundsTest, AbsReflectBoundary) {
    // test a reflect boundary condition between zero and 100
    Expr reflect_x = 100 - cast<int>(abs(100 - (x % 200)));
    Func func = lambda(x, input(reflect_x));
    check(func, input, 0, 101);
}

TEST(BoundsTest, AbsOneSideUndefined) {
    // Verify an undefined bound on one side of the range still results in
    // correct bounds from abs and not an undefined error in the logic or
    // failure to bound the negative branch to zero.
    Func func = lambda(x, input(cast<int>(clamp(abs(1.0f / (x + .1f)), -50, 50))));
    check(func, input, 0, 51);
}
