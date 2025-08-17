#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestWrongDimensionalityExternStage() {
    Func f, g;
    Var x, y;

    g.define_extern("foo", {}, UInt(16), 3);

    // Show throw an error immediately because g was defined with 3 dimensions.
    f(x, y) = cast<float>(g(x, y));
}
}  // namespace

TEST(ErrorTests, WrongDimensionalityExternStage) {
    EXPECT_COMPILE_ERROR(TestWrongDimensionalityExternStage, HasSubstr("TODO"));
}
