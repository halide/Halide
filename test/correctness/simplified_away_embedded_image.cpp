#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(SimplifiedAwayEmbeddedImageTest, Basic) {
    // What happens if an emedded image gets simplified away?
    Buffer<float> input(32, 32);

    Var x("x"), y("y");
    Func foo("foo");

    foo(x, y) = input(x, y) - input(x, y);

    Buffer<float> output(32, 32);

    foo.realize(output);
}
