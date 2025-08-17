#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestSplitNonInnermostPredicated() {
    Func f;
    Var x;
    f(x) = x;
    Var xo, xi, xio, xii;
    // We don't support predicated splits that aren't the innermost loop.
    f.compute_root().split(x, xo, xi, 8, TailStrategy::PredicateStores).split(xi, xio, xii, 9);

    Func g;
    g(x) = f(x);
    g.realize({10});
}
}  // namespace

TEST(ErrorTests, SplitNonInnermostPredicated) {
    EXPECT_COMPILE_ERROR(
        TestSplitNonInnermostPredicated,
        HasSubstr("Cannot split a loop variable resulting from a "
                  "split using PredicateLoads or PredicateStores."));
}
