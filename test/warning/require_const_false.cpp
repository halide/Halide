#include "Halide.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace Halide;
using ::testing::HasSubstr;

TEST(WarningTests, RequireConstFalse) {
    testing::internal::CaptureStderr();

    const int kPrime1 = 7829;
    const int kPrime2 = 7919;

    Var x;
    Func f;
    // choose values that will simplify the require() condition to const-false
    Expr p1 = 1;
    Expr p2 = 2;
    f(x) = require((p1 + p2) == kPrime1,
                   (p1 + p2) * kPrime2,
                   "The parameters should add to exactly", kPrime1, "but were", p1, p2);
    f.compile_jit();

    const std::string captured_stderr = testing::internal::GetCapturedStderr();
    EXPECT_THAT(captured_stderr, HasSubstr("Warning:"));
    EXPECT_THAT(captured_stderr, HasSubstr("This pipeline is guaranteed to fail a require() expression at runtime"));
}
