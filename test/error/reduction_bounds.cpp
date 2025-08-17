#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestReductionBounds() {
    Func f("f"), g("g");
    Var x("x");
    RDom r(0, 100, "r");

    f(x) = x;

    g(x) = 0;
    g(x) = f(g(x - 1)) + r;

    f.compute_at(g, r.x);

    // Use of f is unbounded in g.

    g.realize({100});
}
}  // namespace

TEST(ErrorTests, ReductionBounds) {
    EXPECT_COMPILE_ERROR(
        TestReductionBounds,
        MatchesPattern(R"(In definition of Func \"g(\$\d+)?\":\n)"
                       R"(All of a function's recursive references to itself must )"
                       R"(contain the same pure variables in the same places as on )"
                       R"(the left-hand-side\.)"));
}
