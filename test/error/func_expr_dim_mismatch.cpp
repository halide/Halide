#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestFuncExprDimMismatch() {
    Var x("x"), y("y");
    Func f(Int(32), 1, "f");

    f(x, y) = cast<int>(0);

    f.realize({100, 100});
}
}  // namespace

TEST(ErrorTests, FuncExprDimMismatch) {
    EXPECT_COMPILE_ERROR(TestFuncExprDimMismatch, HasSubstr("TODO"));
}
