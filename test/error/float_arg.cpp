#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestFloatArg() {
    Func f;
    Var x, y;
    f(x, y) = 3 * x + y;

    // Should result in an error
    Func g;
    g(x) = f(f(x, 3) * 17.0f, 3);
}
}  // namespace

TEST(ErrorTests, FloatArg) {
    EXPECT_COMPILE_ERROR(TestFloatArg, HasSubstr("TODO"));
}
