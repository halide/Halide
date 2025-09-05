#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(VectorizedGuardWithIfTailTest, Basic) {
    Var x;

    for (int i = 0; i < 2; i++) {
        Func f, g;
        f(x) = x;
        g(x) = f(x) * 2;

        g.vectorize(x, 8, TailStrategy::GuardWithIf);

        f.compute_at(g, x);

        // A varying amount of f is required depending on if we're in the steady
        // state of g or the tail. Nonetheless, the amount required has a constant
        // upper bound of 8. Vectorization, unrolling, and variants of store_in that
        // require constant extent should all be able to handle this.
        if (i == 0) {
            f.vectorize(x);
        } else {
            f.unroll(x);
        }
        f.store_in(MemoryType::Register);

        Buffer<int> buf = g.realize({37});

        for (int i = 0; i < buf.width(); i++) {
            int correct = i * 2;
            EXPECT_EQ(buf(i), correct) << "buf(" << i << ") = " << buf(i) << " instead of " << correct;
        }
    }
}
