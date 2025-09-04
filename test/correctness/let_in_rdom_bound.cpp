#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(LetInRdomBoundTest, LetExpressionInRDomBound) {
    // RDoms sanitize the input expressions to ensure there are no
    // free variables in them. Check that this doesn't apply to
    // internal variables created by lets.
    Param<int> p;
    Var x;
    RDom r(0, Halide::Internal::Let::make(x.name(), (p + 8) / p, x * x));
    Func f;
    f(x) = 0;
    f(x) += r;

    p.set(3);
    int rdom_bound = (3 + 8) / 3;
    rdom_bound *= rdom_bound;
    Buffer<int> buf = f.realize({10});

    int correct = (rdom_bound * (rdom_bound - 1)) / 2;

    for (int i = 0; i < 10; i++) {
        EXPECT_EQ(buf(i), correct) << "i = " << i;
    }
}
