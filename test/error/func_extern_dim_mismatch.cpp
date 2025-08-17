#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestFuncExternDimMismatch() {
    Var x("x"), y("y");
    Func f(Float(32), 1, "f");
    f.define_extern("test", {}, Float(32), {x, y});
    f.realize({100, 100});
}
}  // namespace

TEST(ErrorTests, FuncExternDimMismatch) {
    EXPECT_COMPILE_ERROR(
        TestFuncExternDimMismatch,
        MatchesPattern(R"(Func \"f(\$\d+)?\" is constrained to have exactly )"
                       R"(1 dimensions, but is defined with 2 dimensions\.)"));
}
