#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("bad_rvar_order", "can't reorder RVars r4$y and r4$x because it may change the meaning of the algorithm.", []() {
        RDom r1(0, 10, 0, 10);

        Func f("f");
        Var x, y;
        f(x, y) = x + y;
        f(r1.x, r1.y) += f(r1.y, r1.x);

        // It's not permitted to change the relative ordering of reduction
        // domain variables when it could change the meaning.
        f.update().reorder(r1.y, r1.x);

        f.realize({10, 10});
    });
}
