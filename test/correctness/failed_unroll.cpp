#include "Halide.h"
#include <cstdlib>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#ifdef _WIN32
#include <windows.h>
#endif

using namespace Halide;

TEST(FailedUnrollTest, Basic) {
    // This tests a temporary hack to silence the error when you try
    // to unroll a loop of non-constant size. We have yet to figure
    // out whether or how to expose this behavior in the scheduling
    // language.
    Func f;
    Var x;
    f(x) = 3;

    // Would normally cause an error, because x doesn't have a known
    // constant size.
    f.unroll(x);

#ifdef _WIN32
    ASSERT_TRUE(SetEnvironmentVariableA("HL_PERMIT_FAILED_UNROLL", "1"))
        << "Failed to set HL_PERMIT_FAILED_UNROLL";
#else
    setenv("HL_PERMIT_FAILED_UNROLL", "1", 1);
#endif

    testing::internal::CaptureStderr();
    EXPECT_NO_THROW(f.realize({17}));
    EXPECT_THAT(
        testing::internal::GetCapturedStderr(),
        testing::HasSubstr("Warning: HL_PERMIT_FAILED_UNROLL is allowing us to "
                           "unroll a non-constant loop into a serial loop. "
                           "Did you mean to do this?"));
}
