#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestFuncExprTypeMismatch() {
    Var x("x"), y("y");
    Func f(Float(32), 1, "f");

    f(x, y) = cast<int>(0);

    f.realize({100, 100});
}
}  // namespace

TEST(ErrorTests, FuncExprTypeMismatch) {
    EXPECT_COMPILE_ERROR(
        TestFuncExprTypeMismatch,
        MatchesPattern(R"(Func \"f(\$\d+)?\" is constrained to only hold values of )"
                       R"(type float32 but is defined with values of type int32\.)"));
}
