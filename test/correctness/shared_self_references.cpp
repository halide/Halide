#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(SharedSelfReferencesTest, Basic) {
    // Check that recursive references get tracked properly
    {
        Func f;
        Var x;
        f(x) = x;
        {
            Expr e = f(2);
            f(0) = e;
            f(1) = e;
        }  // Destroy e
    }  // Destroy f

    // f should have been cleaned up. valgrind will complain if it
    // hasn't been.
}
