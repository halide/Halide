#include "Halide.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace Halide;
using ::testing::HasSubstr;

TEST(WarningTests, SlidingVectors) {
    testing::internal::CaptureStderr();

    Func f, g;
    Var x;

    f(x) = x;

    g(x) = f(x - 1) + f(x + 1);

    Var xo, xi, xii;
    g.split(x, xo, xi, 1024)
        .split(xi, xi, xii, 8)
        .vectorize(xii);

    f.store_at(g, xo)
        .compute_at(g, xi)
        .vectorize(x, 8);

    g.compile_jit();

    const std::string captured_stderr = testing::internal::GetCapturedStderr();
    EXPECT_THAT(captured_stderr, HasSubstr("Warning:"));
    EXPECT_THAT(captured_stderr, HasSubstr("storage folding was not explicitly requested in the schedule"));
}
