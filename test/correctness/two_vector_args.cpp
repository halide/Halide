#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(TwoVectorArgsTest, Basic) {
    Func f, g;
    Var x, y;

    g(x, y) = x + y;

    f(x, y) = g(x, x);

    f.vectorize(x, 4);

    Buffer<int> out = f.realize({4, 4});
}
