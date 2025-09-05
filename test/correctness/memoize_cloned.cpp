#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

namespace {
int call_count = 0;
extern "C" HALIDE_EXPORT_SYMBOL int memoize_cloned_counter(int x) {
    call_count++;
    return x;
}
HalideExtern_1(int, memoize_cloned_counter, int);
}  // namespace

TEST(MemoizeClonedTest, MemoizeCloned) {
    Func f, g, h;
    Var x, y;

    // A clone should use the same cache key as the parent, so that
    // computations of the clone can reuse computations of the
    // parent. This pipeline exploits that to compute f per row of one
    // consumer, then retrieve it from cache per row of another
    // consumer.
    //
    // Setting cache size gives you a trade-off between peak memory
    // usage and recompute.

    f(x, y) = memoize_cloned_counter(x);
    g(x, y) = f(x, y) * 2;
    h(x, y) = f(x, y) + g(x, y);

    h.compute_root();
    g.compute_root();
    f.clone_in(h).compute_at(h, y).memoize();
    f.compute_at(g, y).memoize();

    h.bound(x, 0, 1024).bound(y, 0, 32);

    ASSERT_NO_THROW(h.realize({1024, 32}));
    EXPECT_EQ(call_count, 1024 * 32)
        << "call_count was supposed to be 1024 * 32: " << call_count;

    Internal::JITSharedRuntime::release_all();
}
