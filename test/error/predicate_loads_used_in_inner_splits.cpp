#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestPredicateLoadsUsedInInnerSplits() {
    Func f;
    Var x, xo, xi, xio, xii;
    f(x) = x;
    f.split(x, xo, xi, 2, TailStrategy::Auto)
        .split(xi, xio, xii, 4, TailStrategy::PredicateLoads)
        .reorder(xo, xio, xii);
}
}  // namespace

TEST(ErrorTests, PredicateLoadsUsedInInnerSplits) {
    EXPECT_COMPILE_ERROR(
        TestPredicateLoadsUsedInInnerSplits,
        MatchesPattern(R"(Can't use TailStrategy::PredicateLoads for splitting )"
                       R"(v\d+\.v\d+ in the definition of f\d+\. PredicateLoads may )"
                       R"(not be used to split a Var stemming from the inner Var )"
                       R"(of a prior split\.)"));
}
