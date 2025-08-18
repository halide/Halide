#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(BoundsInferenceTest, Chunk) {
    Func f, g, h;
    Var x, y;

    h(x, y) = x + y;
    g(x, y) = (h(x - 1, y - 1) + h(x + 1, y + 1)) / 2;
    f(x, y) = (g(x - 1, y - 1) + g(x + 1, y + 1)) / 2;

    h.compute_root();
    g.compute_at(f, y);

    // f.trace();

    Buffer<int> out = f.realize({32, 32});

    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 32; x++) {
            ASSERT_EQ(out(x, y), x + y);
        }
    }
}
