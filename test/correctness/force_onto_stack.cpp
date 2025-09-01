#include "Halide.h"
#include "halide_test_error.h"
#include <gtest/gtest.h>

#include <cstdlib>

using namespace Halide;

namespace {
struct TestJITContext : JITUserContext {
    bool saw_malloc{false};
    std::string error_msg;
};

void *my_malloc(JITUserContext *ctx, size_t x) {
    static_cast<TestJITContext *>(ctx)->saw_malloc = true;
    return malloc(x);
}

void my_free(JITUserContext *, void *ptr) {
    free(ptr);
}

void my_error(JITUserContext *ctx, const char *msg) {
    static_cast<TestJITContext *>(ctx)->error_msg = msg;
}

class ForceOntoStackTest : public ::testing::Test {
protected:
    TestJITContext ctx;
    void SetUp() override {
        if (get_jit_target_from_environment().arch == Target::WebAssembly) {
            GTEST_SKIP() << "WebAssembly JIT does not support custom allocators.";
        }
        ctx.handlers.custom_malloc = my_malloc;
        ctx.handlers.custom_free = my_free;
        ctx.handlers.custom_error = my_error;
    }
};
}  // namespace

TEST_F(ForceOntoStackTest, Basic) {
    Func f("f"), g;
    Var x("x"), xo, xi;

    Param<int> p;

    f(x) = x;
    g(x) = f(x);
    g.split(x, xo, xi, p);

    // We need p elements of f per split of g. This could create a
    // dynamic allocation. Instead we'll assert that 8 is enough, so
    // that f can go on the stack and be entirely vectorized.
    f.compute_at(g, xo).bound_extent(x, 8).vectorize(x);

    // Check there's no malloc when the bound is good
    p.set(5);
    g.realize(&ctx, {20});

    EXPECT_FALSE(ctx.saw_malloc) << "There was supposed to be no malloc";

    // Check there was an assertion failure of the appropriate type when the bound is violated
    ctx.handlers.custom_malloc = nullptr;
    ctx.handlers.custom_free = nullptr;
    p.set(10);
    g.realize(&ctx, {20});

    EXPECT_THAT(
        ctx.error_msg,
        MatchesPattern(R"(Bounds given for f(\$\d+)? in x \(from 0 to 7\) do not cover required region \(from 0 to 9\))"));
}

TEST_F(ForceOntoStackTest, TailStrategies) {
    for (TailStrategy tail_strategy : {TailStrategy::GuardWithIf, TailStrategy::Predicate, TailStrategy::PredicateLoads}) {
        // Another way in which a larger static allocation is
        // preferable to a smaller dynamic one is when you compute
        // something at a split guarded by an if. In the very last
        // split (the tail) you don't actually need the whole split's
        // worth of the producer, and indeed asking for it may expand
        // the bounds required of an input image.
        Func f, g;
        Var x, xo, xi;

        f(x) = x;
        g(x) = f(x);
        g.split(x, xo, xi, 8, tail_strategy);

        f.compute_at(g, xo);
        // In the tail case, the amount of g required is min(8, some
        // nasty thing), so we'll add a bound.
        f.bound_extent(x, 8);

        g.realize(&ctx, {20});
        EXPECT_FALSE(ctx.saw_malloc) << "There was supposed to be no malloc";
    }
}
