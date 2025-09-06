#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

namespace {
bool error_occurred = false;
void my_error_handler(JITUserContext *user_context, const char *msg) {
    error_occurred = true;
}
}  // namespace

TEST(ParameterConstraintsTest, SetRangeCalls) {
    // Use explicit set_range() calls
    Func f, g;
    Var x, y;
    Param<float> p;

    Buffer<float> input(100, 100);

    p.set_range(1, 10);

    g(x, y) = input(x, y) + 1.0f;

    g.compute_root();
    f(x, y) = g(cast<int>(x / p), y);

    f.jit_handlers().custom_error = my_error_handler;

    error_occurred = false;
    p.set(2);
    f.realize({100, 100});
    EXPECT_FALSE(error_occurred) << "Error incorrectly raised";

    p.set(0);
    error_occurred = false;
    f.realize({100, 100});
    EXPECT_TRUE(error_occurred) << "Error should have been raised";
}

TEST(ParameterConstraintsTest, ConstructorArguments) {
    // Use ctor arguments
    Func f, g;
    Var x, y;
    // initial value: 2, min: 1, max: 10
    Param<float> p(2, 1, 10);
    Buffer<float> input(100, 100);

    g(x, y) = input(x, y) + 1.0f;

    g.compute_root();
    f(x, y) = g(cast<int>(x / p), y);

    f.jit_handlers().custom_error = my_error_handler;

    error_occurred = false;
    f.realize({100, 100});
    EXPECT_FALSE(error_occurred) << "Error incorrectly raised";

    p.set(0);
    error_occurred = false;
    f.realize({100, 100});
    EXPECT_TRUE(error_occurred) << "Error should have been raised";
}
