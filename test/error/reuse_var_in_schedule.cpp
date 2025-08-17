#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestReuseVarInSchedule() {
    Func f;
    Var x;

    f(x) = x;

    Var xo, xi;
    f.split(x, xo, xi, 4).split(xo, xo, xi, 4);
}
}  // namespace

TEST(ErrorTests, ReuseVarInSchedule) {
    EXPECT_COMPILE_ERROR(TestReuseVarInSchedule, HasSubstr("TODO"));
}
