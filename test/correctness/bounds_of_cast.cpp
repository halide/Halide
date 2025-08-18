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

TEST(BoundsTest, CastToUInt8) {
    // input should only be required from 0 to 256
    Func func = lambda(x, input(cast<uint8_t>(x)));
    check(func, input, 0, 256);
}

TEST(BoundsTest, CastToInt8) {
    Func func = lambda(x, input(cast<int8_t>(x)));
    check(func, input, -128, 256);
}

TEST(BoundsTest, CastToUInt16) {
    Func func = lambda(x, input(cast<uint16_t>(x)));
    check(func, input, 0, 65536);
}

TEST(BoundsTest, CastToInt16) {
    Func func = lambda(x, input(cast<int16_t>(x)));
    check(func, input, -32768, 65536);
}
