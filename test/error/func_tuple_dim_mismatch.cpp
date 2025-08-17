#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;
using namespace Halide::Internal;

namespace {
void TestFuncTupleDimMismatch() {
    Var x("x"), y("y");
    Func f({Int(32), Float(32)}, 1, "f");

    f(x, y) = {cast<int>(0), cast<float>(0)};

    f.realize({100, 100});
}
}  // namespace

TEST(ErrorTests, FuncTupleDimMismatch) {
    EXPECT_COMPILE_ERROR(TestFuncTupleDimMismatch, HasSubstr("TODO"));
}
