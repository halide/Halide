#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("rfactor_inner_dim_non_commutative", "can't perform rfactor() because we can't prove associativity of the operator", []() {
        Func f("f"), g("g");
        Var x("x"), y("y");

        f(x, y) = x + y;
        f.compute_root();

        Param<int> inner_extent, outer_extent;
        RDom r(10, inner_extent, 30, outer_extent);
        inner_extent.set(20);
        outer_extent.set(40);

        g(x, y) = 40;
        g(x, y) -= f(r.x, r.y);

        // Calling rfactor() on the inner dimensions of a non-commutative operator
        // with excluding the outer dimensions like subtraction is not valid as it
        // may change order of computation.
        Var u("u");
        g.update(0).rfactor(r.x, u);
    });
}
