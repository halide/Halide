#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestTooManyArgs() {
    Var x, y;

    Func one_arg;
    one_arg(x) = x * 2;  // One argument

    Func bad_call;
    bad_call(x, y) = one_arg(x, y);  // Called with two

    // Should result in an error
    Buffer<uint32_t> result = bad_call.realize({256, 256});
}
}  // namespace

TEST(ErrorTests, TooManyArgs) {
    EXPECT_COMPILE_ERROR(TestTooManyArgs, MatchesPattern(R"(Func \"f\d+\" was called with 2 arguments, but was defined with 1)"));
}
