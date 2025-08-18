#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(ComputeWithInTest, Basic) {
    Var x("x"), y("y");
    Func one("one"), two("two"), three("three"), output("output");

    one(x, y) = x + y;
    two(x, y) = one(x, y) + 2;
    three(x, y) = one(x, y) + 3;
    output(x, y) = two(x, y) + three(x, y);

    two.compute_root();
    one.in(three).compute_root().compute_with(two, Var::outermost());
    one.compute_root();
    one.compute_at(two, Var::outermost());

    output.realize({64, 64});
}
