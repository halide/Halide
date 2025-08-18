#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(CircularReferenceLeakTest, Basic) {
    // Recursive functions can create circular references. These could
    // cause leaks. Run this test under valgrind to check.
    for (int i = 0; i < 10000; i++) {
        Func f;
        Var x;
        RDom r(0, 10);
        f(x) = x;
        f(r) = f(r - 1) + f(r + 1);
    }
}
