#include "Halide.h"
#include <algorithm>
#include <gtest/gtest.h>

using namespace Halide;

TEST(SplitStoreComputeTest, SplitStoreCompute) {
    Var x("x"), y("y");
    Func f("f"), g("g"), h("h");

    f(x, y) = max(x, y);
    g(x, y) = 17 * f(x, y);
    h(x, y) = (g(x, y - 1) + g(x - 1, y) + g(x, y) + g(x + 1, y) + g(x, y + 1));

    g.store_root();
    g.compute_at(h, y);
    f.compute_root();

    Buffer<int> imh;
    ASSERT_NO_THROW(imh = h.realize({32, 32}));

    for (int i = 0; i < 32; i++) {
        for (int j = 0; j < 32; j++) {
            int v1 = std::max(i - 1, j);
            int v2 = std::max(i + 1, j);
            int v3 = std::max(i, j);
            int v4 = std::max(i, j - 1);
            int v5 = std::max(i, j + 1);
            int correct = 17 * (v1 + v2 + v3 + v4 + v5);

            int val = imh(i, j, 0);
            EXPECT_EQ(val, correct) 
                << "imh(" << i << ", " << j << ") = " << val 
                << " instead of " << correct;
        }
    }
}
