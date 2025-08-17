#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestSpecializeFail() {
    Var x;
    Param<int> p;

    Func f;
    f(x) = x;
    f.specialize(p == 0).vectorize(x, 8);
    f.specialize_fail("Expected failure");

    p.set(42);  // arbitrary nonzero value
    f.realize({100});
}
}  // namespace

TEST(ErrorTests, SpecializeFail) {
    EXPECT_RUNTIME_ERROR(
        TestSpecializeFail,
        HasSubstr("A schedule specialized with specialize_fail() was chosen: "
                  "Expected failure"));
}
