#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

// Tests for regression detailed in https://github.com/halide/Halide/issues/3388

TEST(ComputeAtReorderedUpdateStageTest, Basic) {
    Func g;
    {
        Func f;
        Var x, y;

        f(x, y) = x + y;

        g(x, y) = 0;
        g(x, y) += f(x, y);

        g.update().reorder(y, x);
        f.store_at(g, x).compute_at(g, y);
    }

    Buffer<int> out_orig = g.realize({10, 10});

    // This is here solely to test Halide::Buffer::copy()
    Buffer<int> out = out_orig.copy();

    for (int y = 0; y < out.height(); y++) {
        for (int x = 0; x < out.width(); x++) {
            EXPECT_EQ(out(x, y), x + y);
        }
    }
}
