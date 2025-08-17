#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;
using namespace Halide::Internal;

namespace {
void TestFuncTupleUpdateTypesMismatch() {
    Var x("x"), y("y");
    Func f({UInt(8), Float(64)}, 2, "f");

    f(x, y) = {cast<uint8_t>(0), cast<double>(0)};
    f(x, y) = {cast<int>(0), cast<float>(0)};

    f.realize({100, 100});
}
}  // namespace

TEST(ErrorTests, FuncTupleUpdateTypesMismatch) {
    EXPECT_COMPILE_ERROR(
        TestFuncTupleUpdateTypesMismatch,
        MatchesPattern(R"(In update definition 0 of Func \"f(\$\d+)?\":\n)"
                       R"(Tuple element 0 of update definition has type int32, )"
                       R"(but pure definition has type uint8)"));
}
