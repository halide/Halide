#include "Halide.h"
#include <atomic>
#include <gtest/gtest.h>

using namespace Halide;

namespace {

std::atomic<bool> error_occurred{false};

void halide_error(JITUserContext *ctx, const char *msg) {
    error_occurred = true;
}

}  // namespace

TEST(AssertionFailureTest, InParallelFor) {
    error_occurred = false;

    Func f("f"), g("g"), h("h");
    Var x("x"), y("y"), xi("xi"), yi("yi");

    Param<int> split("split");

    f(x, y) = x + y;
    h(x, y) = f(x, y);
    g(x, y) = h(x % split, y % split) + 1;

    g.tile(x, y, xi, yi, split, split).parallel(y);

    // Force a heap allocation inside the parallel for loop over y.
    f.compute_at(g, y);

    // Make a use of it inside the loop over x
    h.compute_at(g, x);

    // Force an assertion failure inside that for loop if split !=
    // 10. Make sure it fails after the heap allocation of f.
    h.bound(x, 0, 10);

    split.set(11);

    g.jit_handlers().custom_error = halide_error;
    g.realize({40, 40});

    EXPECT_TRUE(error_occurred) << "There was supposed to be an error";
}
