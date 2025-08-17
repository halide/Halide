#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestAsyncRequireFail() {
    const int kPrime1 = 7829;
    const int kPrime2 = 7919;

    Buffer<int> result;
    Param<int> p1, p2;
    Var x;
    Func f, g;
    f(x) = require((p1 + p2) == kPrime1,
                   (p1 + p2) * kPrime2,
                   "The parameters should add to exactly", kPrime1, "but were", p1, p2);
    g(x) = f(x) + f(x + 1);
    f.compute_at(g, x).async();
    // choose values that will fail
    p1.set(1);
    p2.set(2);
    result = g.realize({1});
}
}  // namespace

TEST(ErrorTests, AsyncRequireFail) {
    if (get_jit_target_from_environment().arch == Target::WebAssembly) {
        GTEST_SKIP() << "WebAssembly JIT does not yet support async().";
    }

    EXPECT_RUNTIME_ERROR(TestAsyncRequireFail, MatchesPattern(R"(Requirement Failed: \(false\) 23757 The parameters should add to exactly 7829 but were 1 2)"));
}
