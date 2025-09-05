#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

namespace {
int error_count = 0;
void my_error(JITUserContext *, const char *) {
    error_count++;
}
}  // namespace

TEST(VectorizedAssertTest, VectorizedAssert) {
    error_count = 0;

    Func f("f"), g("g");
    Var x("x");
    Param<int> p;

    f(x) = x;
    f(x) += 1;
    g(x) = f(x) + f(2 * x + p);

    g.vectorize(x, 8);
    f.bound_storage(x, 32);
    // No way to check this at compile time. The size of f depends on both x and
    // p.  An assert is injected, but the assert is inside g's vectorized loop.

    g.jit_handlers().custom_error = my_error;

    ASSERT_NO_THROW(g.compile_jit());

    p.set(256);
    ASSERT_NO_THROW(g.realize({128}));
    EXPECT_EQ(error_count, 1) << "There should have been an error";

    p.set(0);
    ASSERT_NO_THROW(g.realize({8}));
    EXPECT_EQ(error_count, 1) << "There should not have been another error";
}
