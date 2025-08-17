#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestWrapCustomAfterShared() {
    Func f("f"), g1("g1"), g2("g2"), g3("g3"), g4("g4");
    Var x("x"), y("y");

    f(x) = x;
    g1(x, y) = f(x);
    g2(x, y) = f(x);
    g3(x, y) = f(x);

    // It's not valid to call f.in(g1) after defining a shared wrapper for
    // {g1, g2, g3}
    Func wrapper1 = f.in({g1, g4, g3});
    Func wrapper2 = f.in(g3);
}
}  // namespace

TEST(ErrorTests, WrapCustomAfterShared) {
    EXPECT_COMPILE_ERROR(TestWrapCustomAfterShared, MatchesPattern(R"(Redefinition of shared wrapper \[f(\$\d+)? -> f(\$\d+)?_wrapper(\$\d+)?\] in g\d+(\$\d+)? is illegal since g\d+(\$\d+)? shares the same wrapper but is not part of the redefinition)"));
}
