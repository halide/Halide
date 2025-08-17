#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestPointerArithmetic() {
    Param<const char *> p;
    p.set("Hello, world!\n");

    Func f;
    Var x;
    // Should error out during match_types
    f(x) = p + 2;
}
}  // namespace

TEST(ErrorTests, PointerArithmetic) {
    EXPECT_COMPILE_ERROR(TestPointerArithmetic, HasSubstr("TODO"));
}
