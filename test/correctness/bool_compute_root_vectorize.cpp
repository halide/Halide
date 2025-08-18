#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(BoolComputeRootVectorizeTest, Basic) {
    Var x, y;

    Func pred("pred");
    pred(x, y) = x < y;

    Func selector("selector");
    selector(x, y) = select(pred(x, y), 1, 0);

    // Load a vector of 8 bools
    pred.compute_root();
    selector.compute_root().vectorize(x, 8);

    RDom range(0, 100, 0, 100);
    int32_t result = evaluate_may_gpu<int32_t>(sum(selector(range.x, range.y)));

    EXPECT_EQ(result, 4950);
}
