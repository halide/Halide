#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(StreamCompactionTest, StreamCompaction) {
    // A zero-one function:
    Func f;
    Var x;
    f(x) = select((x % 7 == 0) || (x % 5 == 0), 1, 0);
    f.compute_root();

    // Take the cumulative sum. To do this part in parallel see the parallel_reductions test.
    Func cum_sum;
    cum_sum(x) = 0;
    RDom r(0, 1000);
    cum_sum(r + 1) = f(r) + cum_sum(r);
    cum_sum.compute_root();

    // Write out the coordinates of all the ones. We'd use Tuples in the 2d case.
    Func ones;
    ones(x) = -1;  // Initialize to -1 as a sentinel.

    // Figure out which bin each coordinate should go into. Need a
    // clamp so that Halide knows how much space to allocate for ones.
    Expr bin = clamp(cum_sum(r), 0, 1000);

    // In this context, undef means skip writing when f(r) != 1
    ones(bin) = select(f(r) == 1, r, undef<int>());

    // This is actually safe to parallelize, because 'bin' is going to
    // be one-to-one with 'r' when f(r) == 1, but that's too subtle
    // for Halide to prove:
    ones.update().allow_race_conditions().parallel(r, 50);

    Buffer<int> result;
    ASSERT_NO_THROW(result = ones.realize({1001}));
    
    int next = 0;
    for (int i = 0; i < result.width(); i++) {
        EXPECT_EQ(result(i), next) << "i = " << i;
        if (result(i) == next) {
            do {
                next++;
            } while ((next % 5) && (next % 7));
        }
        if (next >= 1000) break;
    }
}
