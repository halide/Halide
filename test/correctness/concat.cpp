#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

namespace {
int count[2];
extern "C" HALIDE_EXPORT_SYMBOL int call_counter_two_slot(int slot, int val) {
    count[slot]++;
    return val;
}
HalideExtern_2(int, call_counter_two_slot, int, int);
}  // namespace

TEST(ConcatTest, BoundsAndCounts) {
    count[0] = count[1] = 0;

    Func f, g, h;
    Var x;

    f(x) = call_counter_two_slot(0, x + 1);
    g(x) = call_counter_two_slot(1, x + 2);
    h(x) = select(x < 100, f(x), g(x));

    // While f and g are loaded over the entire range of h, f only
    // needs to be correct where x < 100, and g only needs to be
    // correct where x >= 100, so there should be a mismatch between
    // bounds computed and bounds allocated.

    f.compute_root();
    g.compute_root();
    h.compute_root();

    Buffer<int> buf = h.realize({200});

    EXPECT_EQ(count[0], 100) << "Incorrect count[0]: " << count[0];
    EXPECT_EQ(count[1], 100) << "Incorrect count[1]: " << count[1];

    for (int i = 0; i < 200; i++) {
        int correct = i < 100 ? i + 1 : i + 2;
        ASSERT_EQ(buf(i), correct) << "buf(" << i << ") = " << buf(i) << " instead of " << correct;
    }
}
