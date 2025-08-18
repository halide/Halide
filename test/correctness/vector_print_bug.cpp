#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(VectorPrintBugTest, Basic) {
    Func f;
    Var x;
    f(x) = print(x);
    f.vectorize(x, 4);
    f.realize({8});
}
