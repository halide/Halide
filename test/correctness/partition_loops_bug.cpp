#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

namespace {
Buffer<double> test(bool with_vectorize) {
    ImageParam input(Float(64), 2);

    Func output;

    Func input_padded = BoundaryConditions::constant_exterior(input, 100);

    RDom rk(-1, 3, -1, 3);

    Var x("x"), y("y");

    output(x, y) = sum(input_padded(x + rk.x, y + rk.y));

    if (with_vectorize) {
        output.vectorize(y, 4);
    }

    Buffer<double> img = lambda(x, y, Expr(1.0)).realize({4, 4});
    input.set(img);

    Buffer<double> result(4, 4);

    input
        .dim(0)
        .set_bounds(0, 4)
        .dim(1)
        .set_bounds(0, 4);
    output.output_buffer()
        .dim(0)
        .set_bounds(0, 4)
        .dim(1)
        .set_bounds(0, 4);

    output.realize(result);

    return result;
}
}  // namespace

TEST(PartitionLoopsBugTest, PartitionLoopsBug) {
    Buffer<double> im1 = test(true);
    Buffer<double> im2 = test(false);

    for (int y = 0; y < im1.height(); y++) {
        for (int x = 0; x < im1.width(); x++) {
            EXPECT_EQ(im1(x, y), im2(x, y)) << "im1(" << x << ", " << y << ") = " << im1(x, y) << ", im2(" << x << ", " << y << ") = " << im2(x, y);
        }
    }
}
