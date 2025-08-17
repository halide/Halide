#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;
using namespace Halide::Internal;

namespace {
void TestFuncTupleTypesMismatch() {
    Var x("x"), y("y");
    Func f({UInt(8), Float(64)}, 2, "f");

    f(x, y) = {cast<int>(0), cast<float>(0)};

    f.realize({100, 100});
}
}  // namespace

TEST(ErrorTests, FuncTupleTypesMismatch) {
    EXPECT_COMPILE_ERROR(TestFuncTupleTypesMismatch, MatchesPattern(R"(Func \"f(\$\d+)?\" is constrained to only hold values of type \(uint\d+, float\d+\) but is defined with values of type \(int\d+, float\d+\)\.)"));
}
