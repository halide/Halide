#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestFuncExprUpdateTypeMismatch() {
    Var x("x"), y("y");
    Func f(Float(32), 2, "f");

    f(x, y) = 0.f;
    f(x, y) = cast<uint8_t>(0);

    f.realize({100, 100});
}
}  // namespace

TEST(ErrorTests, FuncExprUpdateTypeMismatch) {
    EXPECT_COMPILE_ERROR(TestFuncExprUpdateTypeMismatch, MatchesPattern(R"(In update definition 0 of Func \"f(\$\d+)?\":\nTuple element 0 of update definition has type uint\d+, but pure definition has type float\d+)"));
}
