#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;
using namespace Halide::Internal;

namespace {
void check(const Buffer<int> &result) {
    for (int x = 0; x < result.width(); x++) {
        int correct[] = {x, 456, 789, 789};
        for (int c = 0; c < 4; c++) {
            EXPECT_EQ(result(x, c), correct[c]) << "result(" << x << ", " << c << ")";
        }
    }
}
}  // namespace

TEST(MuxTest, DirectMux) {
    Var x("x"), c("c");
    Func f("f");

    f(x, c) = mux(c, {x, 456, 789});

    check(f.realize({100, 4}));
}

TEST(MuxTest, MuxFromTuple) {
    Var x("x"), c("c");
    Func f{"f"}, g{"g"};

    f(x) = {x, 456, 789};
    g(x, c) = mux(c, f(x));

    check(g.realize({100, 4}));
}
