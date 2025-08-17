#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestBadConstCast() {
    Func f;
    Var x;

    // The 256 here would be implicitly cast to uint8, and converted to
    // zero. That's bad. So we check for that inside IROperator.cpp.
    f(x) = cast<uint8_t>(x) % 256;
}
}  // namespace

TEST(ErrorTests, BadConstCast) {
    EXPECT_COMPILE_ERROR(TestBadConstCast, HasSubstr("TODO"));
}
