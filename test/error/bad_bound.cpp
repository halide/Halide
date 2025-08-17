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
    EXPECT_COMPILE_ERROR(TestBadBound, HasSubstr("TODO"));
}
