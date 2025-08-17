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
    EXPECT_COMPILE_ERROR(TestWrapFrozen, HasSubstr("TODO"));
}
