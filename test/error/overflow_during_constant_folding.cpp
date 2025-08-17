#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestOverflowDuringConstantFolding() {
    Func f;
    Var x;
    f(x) = Expr(0x12345678) * Expr(0x76543210);

    f.realize({10});
}
}  // namespace

TEST(ErrorTests, OverflowDuringConstantFolding) {
    EXPECT_COMPILE_ERROR(
        TestOverflowDuringConstantFolding,
        HasSubstr("Signed integer overflow occurred during constant-folding. "
                  "Signed integer overflow for int32 and int64 is undefined "
                  "behavior in Halide."));
}
