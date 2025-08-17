#include "Halide.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace Halide;
using ::testing::HasSubstr;

TEST(WarningTests, HiddenPureDefinition) {
    testing::internal::CaptureStderr();

    Func f;
    Var x;

    f(x) = x;

    // Hide the previous definition.
    f(x) = 2;

    const std::string captured_stderr = testing::internal::GetCapturedStderr();
    EXPECT_THAT(captured_stderr, HasSubstr("Warning:"));
    EXPECT_THAT(captured_stderr, HasSubstr("Update definition completely hides earlier definitions"));
}
