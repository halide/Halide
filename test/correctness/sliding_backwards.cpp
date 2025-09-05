#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

namespace {
extern "C" HALIDE_EXPORT_SYMBOL int sliding_backwards_count(int *counter, int arg) {
    ++*counter;
    return arg;
}
HalideExtern_2(int, sliding_backwards_count, int *, int);
}  // namespace

TEST(SlidingBackwardsTest, Basic) {
    int call_counter = 0;

    Func f, g;
    Var x;
    Param<int *> counter{"counter", &call_counter};

    g(x) = sliding_backwards_count(counter, x);
    f(x) = g(100 - x) + g(100 - x + 1);

    g.compute_at(f, x);
    g.store_root();

    f.realize({10});

    EXPECT_EQ(call_counter, 11) << "g was called " << call_counter << " times instead of 11";
}
