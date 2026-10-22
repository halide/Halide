#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("init_def_should_be_all_vars", "Argument 1 in initial definition of \"f\" is not a Var.\n", []() {
        Buffer<int> in(10, 10);

        Func f("f");
        RDom r(0, in.width(), 0, in.height());
        f(r.x, r.y) = in(r.x, r.y) + 2;
        f.realize({in.width(), in.height()});
    });
}
