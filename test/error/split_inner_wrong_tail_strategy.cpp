#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestSplitInnerWrongTailStrategy() {
    Func f;
    Var x;
    f(x) = x;
    f(x) += 1;
    Var xo, xi, xio, xii;
    // Would redundantly redo some +=1, and create incorrect output.
    f.compute_root();
    f.update().split(x, xo, xi, 8).split(xi, xio, xii, 9, TailStrategy::RoundUp);

    Func g;
    g(x) = f(x);
    g.realize({10});
}
}  // namespace

TEST(ErrorTests, SplitInnerWrongTailStrategy) {
    EXPECT_COMPILE_ERROR(
        TestSplitInnerWrongTailStrategy,
        MatchesPattern(R"(Can't use TailStrategy::RoundUp for splitting v\d+\.v\d+ )"
                       R"(in update definition of f\d+\.update\(0\)\. It may )"
                       R"(redundantly recompute some values, which could change )"
                       R"(the meaning of the algorithm\. Use )"
                       R"(TailStrategy::GuardWithIf instead.)"));
}
