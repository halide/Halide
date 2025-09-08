#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;
using testing::Combine;
using testing::ValuesIn;
using testing::internal::ParamGenerator;

namespace {
size_t largest_allocation = 0;

void *my_malloc(JITUserContext *user_context, size_t x) {
    largest_allocation = std::max(x, largest_allocation);
    void *orig = malloc(x + 32);
    void *ptr = (void *)((((size_t)orig + 32) >> 5) << 5);
    ((void **)ptr)[-1] = orig;
    return ptr;
}

void my_free(JITUserContext *user_context, void *ptr) {
    free(((void **)ptr)[-1]);
}

void check(Func out, std::vector<TailStrategy> tails) {
    bool has_round_up =
        std::find(tails.begin(), tails.end(), TailStrategy::RoundUp) != tails.end() ||
        std::find(tails.begin(), tails.end(), TailStrategy::RoundUpAndBlend) != tails.end() ||
        std::find(tails.begin(), tails.end(), TailStrategy::PredicateLoads) != tails.end() ||
        std::find(tails.begin(), tails.end(), TailStrategy::PredicateStores) != tails.end();
    bool has_shift_inwards =
        std::find(tails.begin(), tails.end(), TailStrategy::ShiftInwards) != tails.end() ||
        std::find(tails.begin(), tails.end(), TailStrategy::ShiftInwardsAndBlend) != tails.end();

    std::vector<int> sizes_to_try;

    // A size that's a multiple of all the splits should always be
    // exact
    sizes_to_try.push_back(1024);

    // Sizes larger than any of the splits should be fine if we don't
    // have any roundups. The largest split we have is 128
    if (!has_round_up) {
        sizes_to_try.push_back(130);
    }

    // Tiny sizes are fine if we only have GuardWithIf
    if (!has_round_up && !has_shift_inwards) {
        sizes_to_try.push_back(3);
    }

    out.jit_handlers().custom_malloc = my_malloc;
    out.jit_handlers().custom_free = my_free;

    for (int s : sizes_to_try) {
        largest_allocation = 0;
        out.realize({s});
        size_t expected = (s + 1) * 4;
        size_t tolerance = 3 * sizeof(int);
        EXPECT_LE(largest_allocation, expected + tolerance);
    }
}

// Test random compositions of tail strategies in simple
// producer-consumer pipelines. The bounds being tight sometimes
// depends on the simplifier being able to cancel out things.

const ParamGenerator<TailStrategy> tails =
    ValuesIn({TailStrategy::RoundUp,
              TailStrategy::GuardWithIf,
              TailStrategy::ShiftInwards,
              TailStrategy::RoundUpAndBlend,
              TailStrategy::ShiftInwardsAndBlend});

const ParamGenerator<TailStrategy> innermost_tails =
    ValuesIn({TailStrategy::RoundUp,
              TailStrategy::GuardWithIf,
              TailStrategy::PredicateLoads,
              TailStrategy::PredicateStores,
              TailStrategy::ShiftInwards,
              TailStrategy::RoundUpAndBlend,
              TailStrategy::ShiftInwardsAndBlend});

using TailStrategyX2 = std::tuple<TailStrategy, TailStrategy>;
using TailStrategyX3 = std::tuple<TailStrategy, TailStrategy, TailStrategy>;
using TailStrategyX4 = std::tuple<TailStrategy, TailStrategy, TailStrategy, TailStrategy>;

class NestedTailStrategiesTest : public testing::Test {
protected:
    void SetUp() override {
        if (get_jit_target_from_environment().arch == Target::WebAssembly) {
            GTEST_SKIP() << "WebAssembly JIT does not support custom allocators.";
        }
    }
};
class NestedTailStrategiesTwoStages : public NestedTailStrategiesTest, public testing::WithParamInterface<TailStrategyX2> {};
class NestedTailStrategiesThreeStageChain : public NestedTailStrategiesTest, public testing::WithParamInterface<TailStrategyX3> {};
class NestedTailStrategiesOneOuterTwoInner : public NestedTailStrategiesTest, public testing::WithParamInterface<TailStrategyX3> {};
class NestedTailStrategiesInnerOuterInnerOuter : public NestedTailStrategiesTest, public testing::WithParamInterface<TailStrategyX4> {};

}  // namespace

TEST_P(NestedTailStrategiesTwoStages, Check) {
    // Two stages. First stage computed at tiles of second.
    const auto [t1, t2] = GetParam();
    Func in, f, g;
    Var x;

    in(x) = x;
    f(x) = in(x);
    g(x) = f(x);

    Var xo, xi;
    g.split(x, xo, xi, 64, t1);
    f.compute_at(g, xo).split(x, xo, xi, 8, t2);
    in.compute_root();

    check(g, {t1, t2});
}

TEST_P(NestedTailStrategiesThreeStageChain, Check) {
    // Three stages. First stage computed at tiles of second, second
    // stage computed at tiles of third.
    const auto [t1, t2, t3] = GetParam();
    Func in("in"), f("f"), g("g"), h("h");
    Var x;

    in(x) = x;
    f(x) = in(x);
    g(x) = f(x);
    h(x) = g(x);

    Var xo, xi;
    h.split(x, xo, xi, 64, t1);
    g.compute_at(h, xo).split(x, xo, xi, 16, t2);
    f.compute_at(g, xo).split(x, xo, xi, 4, t3);
    in.compute_root();

    check(h, {t1, t2, t3});
}

TEST_P(NestedTailStrategiesOneOuterTwoInner, Check) {
    // Three stages. First stage computed at tiles of third, second
    // stage computed at smaller tiles of third.
    const auto [t1, t2, t3] = GetParam();
    Func in, f, g, h;
    Var x;

    in(x) = x;
    f(x) = in(x);
    g(x) = f(x);
    h(x) = g(x);

    Var xo, xi, xii, xio;
    h.split(x, xo, xi, 128, t1).split(xi, xio, xii, 64);
    g.compute_at(h, xio).split(x, xo, xi, 8, t2);
    f.compute_at(h, xo).split(x, xo, xi, 8, t3);
    in.compute_root();

    check(h, {t1, t2, t3});
}

TEST_P(NestedTailStrategiesInnerOuterInnerOuter, Check) {
    // Same as above, but the splits on the output are composed in
    // reverse order so we don't get a perfect split on the inner one
    // (but can handle smaller outputs).
    const auto [t1, t2, t3, t4] = GetParam();
    Func in("in"), f("f"), g("g"), h("h");
    Var x;

    in(x) = x;
    f(x) = in(x);
    g(x) = f(x);
    h(x) = g(x);

    Var xo, xi, xoo, xoi;
    h.split(x, xo, xi, 64, t1).split(xo, xoo, xoi, 2, t2);
    g.compute_at(h, xoi).split(x, xo, xi, 8, t3);
    f.compute_at(h, xoo).split(x, xo, xi, 8, t4);
    in.compute_root();

    check(h, {t1, t2, t3, t4});
}

INSTANTIATE_TEST_SUITE_P(
    Strategy, NestedTailStrategiesTwoStages,
    ::testing::Combine(innermost_tails, innermost_tails));

INSTANTIATE_TEST_SUITE_P(
    Strategy, NestedTailStrategiesThreeStageChain,
    ::testing::Combine(innermost_tails, innermost_tails, innermost_tails));

INSTANTIATE_TEST_SUITE_P(
    Strategy, NestedTailStrategiesOneOuterTwoInner,
    ::testing::Combine(tails, innermost_tails, innermost_tails));

INSTANTIATE_TEST_SUITE_P(
    Strategy, NestedTailStrategiesInnerOuterInnerOuter,
    ::testing::Combine(innermost_tails, tails, innermost_tails, tails));
