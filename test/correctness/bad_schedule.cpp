#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("bad_schedule", "Cannot vectorize dimension x of function f because the function is scheduled inline.", []() {
        Func f, g;
        Var x, y;

        f(x) = x;
        g(x) = f(x);

        // f is inlined, so this schedule is bad.
        f.vectorize(x, 4);

        g.realize({10});
    });
}
