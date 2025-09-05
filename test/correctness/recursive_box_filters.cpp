#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

// Compute a two-tap and a four-tap box filter at the same time,
// recursively.
TEST(RecursiveBoxFiltersTest, RecursiveBoxFilters) {
    Var x;
    Func f;
    f(x) = x;
    f.compute_root();

    const int size = 1024;

    Func h;
    h(x) = {undef<int>(), undef<int>()};
    h(0) = {f(0), f(0)};
    h(1) = {f(1) + f(0), f(1) + f(0)};

    RDom r(2, size - 2);
    Expr blur2 = f(r) + f(r - 1);
    h(r) = {blur2, blur2 + h(r - 2)[0]};

    // This is safe to vectorize, but it's not associative/commutative, so we
    // have to pass 'true' to the atomic call to tell it to skip the check.
    h.update(2).atomic(true).vectorize(r, 16);

    // These stages don't need scheduling
    h.update(0).unscheduled();
    h.update(1).unscheduled();

    Buffer<int> r0(size);
    Buffer<int> r1(size);
    ASSERT_NO_THROW(h.realize({r0, r1}));

    for (int i = 3; i < size; i++) {
        int correct2 = i + (i - 1);
        int correct4 = i + (i - 1) + (i - 2) + (i - 3);
        EXPECT_EQ(r0(i), correct2) << "r0[" << i << "]";
        EXPECT_EQ(r1(i), correct4) << "r1[" << i << "]";
    }
}
