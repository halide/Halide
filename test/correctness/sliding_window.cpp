#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

namespace {
extern "C" HALIDE_EXPORT_SYMBOL int sliding_window_call_counter(int x, int y, int *count) {
    ++*count;
    return 0;
}
HalideExtern_3(int, sliding_window_call_counter, int, int, int *);

class SlidingWindowTest : public ::testing::Test {
protected:
    Target target{get_jit_target_from_environment()};
    Var x{"x"}, y{"y"};
    int count{0};
    Param<int *> p_count{"count", &count};

    void SetUp() override {
        if (target.arch == Target::WebAssembly) {
            GTEST_SKIP() << "WebAssembly JIT does not support custom allocators";
        }
        count = 0;
    }
};

class SlidingWindowTestP : public SlidingWindowTest, public ::testing::WithParamInterface<MemoryType> {};
INSTANTIATE_TEST_SUITE_P(MemoryType, SlidingWindowTestP, ::testing::Values(MemoryType::Heap, MemoryType::Register));
}  // namespace

TEST_P(SlidingWindowTestP, BasicSlidingWindowWithSpecialization) {
    MemoryType store_in = GetParam();

    Func f, g;

    f(x) = sliding_window_call_counter(x, 0, p_count);
    g(x) = f(x) + f(x - 1);

    f.store_root().compute_at(g, x).store_in(store_in);

    // Test that sliding window works when specializing.
    g.specialize(g.output_buffer().dim(0).min() == 0);

    ASSERT_NO_THROW(g.realize({100}));
    EXPECT_EQ(count, 101) << "f should be able to tell that it only needs to compute each value once";
}

TEST_P(SlidingWindowTestP, TwoProducersOneConsumer) {
    // Try two producers used by the same consumer.
    MemoryType store_in = GetParam();

    Func f, g, h;

    f(x) = sliding_window_call_counter(2 * x + 0, 0, p_count);
    g(x) = sliding_window_call_counter(2 * x + 1, 0, p_count);
    h(x) = f(x) + f(x - 1) + g(x) + g(x - 1);

    f.store_root().compute_at(h, x).store_in(store_in);
    g.store_root().compute_at(h, x).store_in(store_in);

    ASSERT_NO_THROW(h.realize({100}));
    EXPECT_EQ(count, 202);
}

TEST_P(SlidingWindowTestP, SequenceOfTwoSlidingWindows) {
    // Try a sequence of two sliding windows.
    MemoryType store_in = GetParam();

    Func f, g, h;

    f(x) = sliding_window_call_counter(2 * x + 0, 0, p_count);
    g(x) = f(x) + f(x - 1);
    h(x) = g(x) + g(x - 1);

    f.store_root().compute_at(h, x).store_in(store_in);
    g.store_root().compute_at(h, x).store_in(store_in);

    ASSERT_NO_THROW(h.realize({100}));
    int correct = store_in == MemoryType::Register ? 103 : 102;
    EXPECT_EQ(count, correct);
}

TEST_P(SlidingWindowTestP, SlidingWindowWithContainingStage) {
    // Try again where there's a containing stage
    MemoryType store_in = GetParam();

    Func f, g, h;
    f(x) = sliding_window_call_counter(x, 0, p_count);
    g(x) = f(x) + f(x - 1);
    h(x) = g(x);

    f.store_root().compute_at(g, x).store_in(store_in);
    g.compute_at(h, x);

    ASSERT_NO_THROW(h.realize({100}));
    EXPECT_EQ(count, 101);
}

TEST_P(SlidingWindowTestP, SlidingWindowWithVectorizedInnerDimension) {
    // Add an inner vectorized dimension.
    MemoryType store_in = GetParam();

    Func f, g, h;
    Var c;
    f(x, c) = sliding_window_call_counter(x, c, p_count);
    g(x, c) = f(x + 1, c) - f(x, c);
    h(x, c) = g(x, c);

    f.store_root()
        .compute_at(h, x)
        .store_in(store_in)
        .reorder(c, x)
        .reorder_storage(c, x)
        .bound(c, 0, 4)
        .vectorize(c);

    g.compute_at(h, x);

    h.reorder(c, x).reorder_storage(c, x).bound(c, 0, 4).vectorize(c);

    ASSERT_NO_THROW(h.realize({100, 4}));
    EXPECT_EQ(count, 404);
}

TEST_F(SlidingWindowTest, SlidingWindowWithReduction) {
    // Now try with a reduction
    RDom r(0, 100);
    Func f, g;

    f(x, y) = 0;
    f(r, y) = sliding_window_call_counter(r, y, p_count);
    f.store_root().compute_at(g, y);

    g(x, y) = f(x, y) + f(x, y - 1);

    ASSERT_NO_THROW(g.realize({10, 10}));

    // For each value of y, f should be evaluated over (0 .. 100) in
    // x, and (y .. y-1) in y. Sliding window optimization means that
    // we can skip the y-1 case in all but the first iteration.
    EXPECT_EQ(count, 100 * 11);
}

TEST_F(SlidingWindowTest, SlidingOverMultipleDimensions) {
    // Now try sliding over multiple dimensions at once
    Func f, g;

    f(x, y) = sliding_window_call_counter(x, y, p_count);
    g(x, y) = f(x - 1, y) + f(x, y) + f(x, y - 1);
    f.store_root().compute_at(g, x);

    ASSERT_NO_THROW(g.realize({10, 10}));
    EXPECT_EQ(count, 11 * 11);
}

TEST_F(SlidingWindowTest, DiagonalSlidingNotSupported) {
    Func f, g;

    // Now a trickier example. In order for this to work, Halide would have to slide diagonally. We don't handle this.
    f(x, y) = sliding_window_call_counter(x, y, p_count);
    // When x was two smaller the second term was computed. When y was two smaller the third term was computed.
    g(x, y) = f(x + y, x - y) + f((x - 2) + y, (x - 2) - y) + f(x + (y - 2), x - (y - 2));
    f.store_root().compute_at(g, x);

    ASSERT_NO_THROW(g.realize({10, 10}));
    EXPECT_EQ(count, 1500);
}

TEST_F(SlidingWindowTest, StackAllocationInsteadOfMalloc) {
    // Now make sure Halide folds the example in Func.h down to a stack allocation
    Func f, g;
    f(x, y) = x * y;
    g(x, y) = f(x, y) + f(x + 1, y) + f(x, y + 1) + f(x + 1, y + 1);
    f.store_at(g, y).compute_at(g, x);
    g.jit_handlers().custom_malloc = [](JITUserContext *, size_t x) {
        ADD_FAILURE() << "Malloc wasn't supposed to be called!";
        return malloc(x);
    };

    ASSERT_NO_THROW(g.realize({10, 10}));
}

TEST_F(SlidingWindowTest, FixedFootprintOverLoopVar) {
    // Sliding where the footprint is actually fixed over the loop
    // var. Everything in the producer should be computed in the
    // first iteration.
    Func f, g;

    f(x) = sliding_window_call_counter(x, 0, p_count);
    g(x) = f(0) + f(5);

    f.store_root().compute_at(g, x);

    ASSERT_NO_THROW(g.realize({100}));
    EXPECT_EQ(count, 6) << "f should be able to tell that it only needs to compute each value once";
}

TEST_P(SlidingWindowTestP, NewValueEveryThirdIteration) {
    // Sliding where we only need a new value every third iteration of the consumer.
    MemoryType store_in = GetParam();

    Func f, g;

    f(x) = sliding_window_call_counter(x, 0, p_count);
    g(x) = f(x / 3);

    f.store_root().compute_at(g, x).store_in(store_in);

    ASSERT_NO_THROW(g.realize({100}));
    EXPECT_EQ(count, 34) << "f should be able to tell that it only needs to compute each value once";
}

TEST_P(SlidingWindowTestP, ExcessiveBoundsCheck) {
    // Sliding where we only need a new value every third iteration of the consumer.
    // This test checks that we don't ask for excessive bounds.
    MemoryType store_in = GetParam();

    ImageParam f(Int(32), 1);
    Func g;

    g(x) = f(x / 3);

    Var xo;
    g.split(x, xo, x, 10);
    f.in().store_at(g, xo).compute_at(g, x).store_in(store_in);

    Buffer<int> buf(33);
    f.set(buf);

    ASSERT_NO_THROW(g.realize({98}));
}

TEST_P(SlidingWindowTestP, SlidingWithUnrolledProducer) {
    // Sliding with an unrolled producer
    MemoryType store_in = GetParam();

    Var x, xi;
    Func f, g;

    f(x) = sliding_window_call_counter(x, 0, p_count) + x * x;
    g(x) = f(x) + f(x - 1);

    g.split(x, x, xi, 10);
    f.store_root().compute_at(g, x).store_in(store_in).unroll(x);

    ASSERT_NO_THROW(g.realize({100}));
    EXPECT_EQ(count, 101);
}

TEST_F(SlidingWindowTest, SlidingWithVectorizedProducerAndConsumer) {
    // Sliding with a vectorized producer and consumer.
    Func f, g;
    f(x) = sliding_window_call_counter(x, 0, p_count);
    g(x) = f(x + 1) + f(x - 1);

    f.store_root().compute_at(g, x).vectorize(x, 4);
    g.vectorize(x, 4);

    ASSERT_NO_THROW(g.realize({100}));
    EXPECT_EQ(count, 104);
}

TEST_F(SlidingWindowTest, VectorizedRotationInRegisters) {
    // Sliding with a vectorized producer and consumer, trying to rotate
    // cleanly in registers.
    Func f, g;
    f(x) = sliding_window_call_counter(x, 0, p_count);
    g(x) = f(x + 1) + f(x - 1);

    // This currently requires a trick to get everything to be aligned
    // nicely. This exploits the fact that ShiftInwards splits are
    // aligned to the end of the original loop (and extending before the
    // min if necessary).
    Var xi("xi");
    f.store_root().compute_at(g, x).store_in(MemoryType::Register).split(x, x, xi, 8).vectorize(xi, 4).unroll(xi);
    g.vectorize(x, 4, TailStrategy::RoundUp);

    ASSERT_NO_THROW(g.realize({100}));
    EXPECT_EQ(count, 102);
}

TEST_F(SlidingWindowTest, SequenceOfStencilsComputedAtOutput) {
    // A sequence of stencils, all computed at the output.
    Func f, g, h, u, v;
    f(x, y) = sliding_window_call_counter(x, y, p_count);
    g(x, y) = f(x, y - 1) + f(x, y + 1);
    h(x, y) = g(x - 1, y) + g(x + 1, y);
    u(x, y) = h(x, y - 1) + h(x, y + 1);
    v(x, y) = u(x - 1, y) + u(x + 1, y);

    u.compute_at(v, y);
    h.store_root().compute_at(v, y);
    g.store_root().compute_at(v, y);
    f.store_root().compute_at(v, y);

    ASSERT_NO_THROW(v.realize({10, 10}));
    EXPECT_EQ(count, 14 * 14);
}

TEST_F(SlidingWindowTest, SequenceOfStencilsSlidingComputedAtOutput) {
    // A sequence of stencils, sliding computed at the output.
    Func f, g, h, u, v;
    f(x, y) = sliding_window_call_counter(x, y, p_count);
    g(x, y) = f(x, y - 1) + f(x, y + 1);
    h(x, y) = g(x - 1, y) + g(x + 1, y);
    u(x, y) = h(x, y - 1) + h(x, y + 1);
    v(x, y) = u(x - 1, y) + u(x + 1, y);

    u.compute_at(v, y);
    h.store_root().compute_at(v, y);
    g.compute_at(h, y);
    f.store_root().compute_at(v, y);

    ASSERT_NO_THROW(v.realize({10, 10}));
    EXPECT_EQ(count, 14 * 14);
}

TEST_F(SlidingWindowTest, BoundaryConditionBeforeLoopStart) {
    // Sliding a func that has a boundary condition before the beginning
    // of the loop. This needs an explicit warmup before we start sliding.
    Func f, g;
    f(x) = sliding_window_call_counter(x, 0, p_count);
    g(x) = f(max(x, 3));

    f.store_root().compute_at(g, x);

    ASSERT_NO_THROW(g.realize({10}));
    EXPECT_EQ(count, 7);
}

TEST_F(SlidingWindowTest, BoundaryConditionOnBothSides) {
    // Sliding a func that has a boundary condition on both sides.
    Func f, g, h;
    f(x) = sliding_window_call_counter(x, 0, p_count);
    g(x) = f(clamp(x, 0, 9));
    h(x) = g(x - 1) + g(x + 1);

    f.store_root().compute_at(h, x);
    g.store_root().compute_at(h, x);

    ASSERT_NO_THROW(h.realize({10}));
    EXPECT_EQ(count, 10);
}
