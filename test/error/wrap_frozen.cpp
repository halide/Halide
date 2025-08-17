#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestWrapFrozen() {
    Func f("f"), g("g");
    Var x("x"), y("y");

    f(x) = x;
    g(x) = f(x);
    Func wrapper = f.in(g);
    wrapper(x) += 1;
}
}  // namespace

TEST(ErrorTests, WrapFrozen) {
    EXPECT_COMPILE_ERROR(TestWrapFrozen, MatchesPattern(R"(Func f(\$\d+)?_in_g(\$\d+)?\$0 cannot be given a new update definition, because it has already been realized or used in the definition of another Func\.)"));
}
