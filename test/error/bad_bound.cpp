#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestBadBound() {
    Func f("f");
    Var x("x"), y("y");

    f(x) = 0;
    f.bound(y, 0, 10);
}
}  // namespace

TEST(ErrorTests, BadBound) {
    EXPECT_COMPILE_ERROR(
        TestBadBound,
        MatchesPattern(R"(Can't bound variable y of function f(\$\d+)? )"
                       R"(because y is not one of the pure variables of )"
                       R"(f(\$\d+)?\.)"));
}
