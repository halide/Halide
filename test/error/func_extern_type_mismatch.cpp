#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;
using namespace Halide::Internal;

namespace {
void TestFuncExternTypeMismatch() {
    Var x("x"), y("y");
    Func f({UInt(8), Float(64)}, 2, "f");
    f.define_extern("test", {}, {Int(32), Float(32)}, {x, y});
    f.realize({100, 100});
}
}  // namespace

TEST(ErrorTests, FuncExternTypeMismatch) {
    EXPECT_COMPILE_ERROR(
        TestFuncExternTypeMismatch,
        MatchesPattern(R"(Func \"f(\$\d+)?\" is constrained to only hold values of )"
                       R"(type \(uint8, float64\) but is defined with values of )"
                       R"(type \(int32, float32\)\.)"));
}
