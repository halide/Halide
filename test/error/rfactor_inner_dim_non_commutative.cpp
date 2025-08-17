#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestRfactorInnerDimNonCommutative() {
    Func f("f"), g("g");
    Var x("x"), y("y");

    f(x, y) = x + y;
    f.compute_root();

    Param<int> inner_extent, outer_extent;
    RDom r(10, inner_extent, 30, outer_extent);
    inner_extent.set(20);
    outer_extent.set(40);

    g(x, y) = 40;
    g(x, y) -= f(r.x, r.y);

    // Calling rfactor() on the inner dimensions of a non-commutative operator
    // with excluding the outer dimensions like subtraction is not valid as it
    // may change order of computation.
    Var u("u");
    g.update(0).rfactor(r.x, u);
}
}  // namespace

TEST(ErrorTests, RfactorInnerDimNonCommutative) {
    EXPECT_COMPILE_ERROR(
        TestRfactorInnerDimNonCommutative,
        MatchesPattern(R"(In schedule for g(\$\d+)?\.update\(0\): can't perform )"
                       R"(rfactor\(\) because we can't prove associativity of the )"
                       R"(operator\n)"
                       R"(Vars: r\d+\$x r\d+\$y x y __outermost)"));
}
