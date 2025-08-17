#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestClampOutOfRange() {
    Var x;
    Func f;

    f(x) = clamp(cast<int8_t>(x), 0, 255);
    Buffer<> result = f.realize({42});
}
}  // namespace

TEST(ErrorTests, ClampOutOfRange) {
    EXPECT_COMPILE_ERROR(TestClampOutOfRange, MatchesPattern(R"(Type mismatch in call to clamp\. First argument \(int\d+\(v\d+\)\) has type int\d+, but third argument \(255\) has type int\d+\. Use an explicit cast\.)"));
}
