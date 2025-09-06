#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(NamedUpdatesTest, NamedUpdates) {
    // Test various possible pieces of syntax for tracking the various
    // definitions of a Func. Mostly we just want to make sure they
    // compile.
    RDom r(0, 16);

    Func f;
    Var x;
    Func ref;

    {
        Stage pure =
            f(x) = x;

        Stage fix_first =
            f(0) = 1;

        Stage rewrites[] = {
            f(r * 2) = 13,
            f(r * 4) = 14};

        struct {
            Stage a, b, c;
        } more_updates = {
            f(3 * r) = 4,
            f(2 * r) = 8,
            f(5 * r) = 2};

        f.compute_root();

        pure.vectorize(x, 4);
        rewrites[0].parallel(r);
        rewrites[1].vectorize(r, 4);
        more_updates.a.vectorize(r, 4);
        more_updates.b.vectorize(r, 4);
        more_updates.c.vectorize(r, 4);

        f.update().unscheduled();  // fix_first isn't scheduled
    }

    // Define the same thing without all the weird syntax and without
    // any scheduling.
    {
        ref(x) = x;
        ref(0) = 1;
        ref(r * 2) = 13;
        ref(r * 4) = 14;
        ref(3 * r) = 4;
        ref(2 * r) = 8;
        ref(5 * r) = 2;
    }

    Buffer<int> result, result_ref;
    ASSERT_NO_THROW(result = f.realize({128}));
    ASSERT_NO_THROW(result_ref = ref.realize({128}));

    RDom check(result);
    uint32_t error = evaluate<uint32_t>(
        maximum(abs(result(check) - result_ref(check))));

    EXPECT_EQ(error, 0) << "There was a difference between using named updates and not.";
}
