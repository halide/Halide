#include "Halide.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace Halide;
using ::testing::HasSubstr;

TEST(WarningTests, UnscheduledUpdateDef) {
    testing::internal::CaptureStderr();

    Func f;
    Var x;

    f(x) = 0;
    f(x) += 5;

    f.vectorize(x, 8);

    f.compile_jit();

    const std::string captured_stderr = testing::internal::GetCapturedStderr();
    EXPECT_THAT(captured_stderr, HasSubstr("Warning:"));
    EXPECT_THAT(captured_stderr, HasSubstr("You may have forgotten to schedule it."));
}
