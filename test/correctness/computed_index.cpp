#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(ComputedIndexTest, Basic) {
    Buffer<uint8_t> in1(256, 256);
    Buffer<uint8_t> in2(256, 256, 10);

    Func f;
    Var x, y;

    f(x, y) = in2(x, y, clamp(in1(x, y), 0, 9));
    Buffer<uint8_t> out = f.realize({256, 256});
}
