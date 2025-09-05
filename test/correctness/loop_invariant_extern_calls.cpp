#include "Halide.h"
#include <gtest/gtest.h>

#include <atomic>

using namespace Halide;

namespace {
// TODO: in C++20, use std::atomic_ref. Cleaner.
extern "C" HALIDE_EXPORT_SYMBOL int liec_func(std::atomic<int> *counter, int x) {
    ++*counter;
    return x;
}
HalidePureExtern_2(int, liec_func, std::atomic<int> *, int);

extern "C" HALIDE_EXPORT_SYMBOL int liec_impure(std::atomic<int> *counter, int x) {
    ++*counter;
    return x;
}
HalideExtern_2(int, liec_impure, std::atomic<int> *, int);
}  // namespace

TEST(LoopInvariantExternCalls, PureLoops) {
    Var x, y;

    std::atomic invariant{0}, only_y{0}, both_xy{0};
    Param<std::atomic<int> *> invariant_counter{"invariant_counter", &invariant};
    Param<std::atomic<int> *> only_y_counter{"only_y", &only_y};
    Param<std::atomic<int> *> both_xy_counter{"both_xy", &both_xy};

    Func f;
    f(x, y) = liec_func(invariant_counter, Expr(0)) + liec_func(only_y_counter, y) +
              liec_func(both_xy_counter, x * 32 + y);

    // llvm rightly refuses to lift loop invariants out of loops that
    // might have an extent of zero. It's possibly wasted work.
    f.bound(x, 0, 32).bound(y, 0, 32);

    Buffer<int> im = f.realize({32, 32});

    // Check the result was what we expected
    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 32; x++) {
            int correct = y + 32 * x + y;
            EXPECT_EQ(im(x, y), correct) << "x = " << x << ", y = " << y;
        }
    }

    // Check the call counters
    EXPECT_EQ(invariant, 1);
    EXPECT_EQ(only_y, 32);
    EXPECT_EQ(both_xy, 32 * 32);
}

// Note that pure things get lifted out of loops (even parallel ones), but impure things do not.
TEST(LoopInvariantExternCalls, LiftPureNotImpure) {
    Var x, y;

    std::atomic pure_call_counter{0}, impure_call_counter{0};
    Param<std::atomic<int> *> pure_counter{"pure_counter", &pure_call_counter};
    Param<std::atomic<int> *> impure_counter{"impure_counter", &impure_call_counter};

    Func g;
    g(x, y) = liec_func(pure_counter, Expr(0)) + liec_impure(impure_counter, Expr(0));
    g.parallel(y);
    g.realize({32, 32});

    EXPECT_EQ(pure_call_counter, 1);
    EXPECT_EQ(impure_call_counter, 32 * 32);
}

TEST(LoopInvariantExternCalls, GPU) {
    // Check that something we can't compute on the GPU gets lifted
    // out of the GPU loop. This code would fail to link if we didn't
    // do loop invariant code motion.
    if (!get_jit_target_from_environment().has_gpu_feature()) {
        GTEST_SKIP() << "No GPU target available";
    }
    std::atomic call_counter{0};
    Param<std::atomic<int> *> counter{"counter", &call_counter};

    Var x, y;
    Func h;
    h(x, y) = liec_func(counter, Expr(0));

    Var xi, yi;
    h.gpu_tile(x, y, xi, yi, 8, 8);
    h.realize({32, 32});

    EXPECT_EQ(call_counter, 1);
}
