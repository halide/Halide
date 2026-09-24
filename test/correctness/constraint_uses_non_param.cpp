#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("constraint_uses_non_param", "refers to Var or RVar x", []() {
        Func f, g;
        Var x, y;
        f(x, y) = 0;
        g(x, y) = f(x, y);
        Pipeline p(g);

        // This can't possibly be a precondition
        p.add_requirement(x == 4 && f(3, 2) == 5);
    });
}
