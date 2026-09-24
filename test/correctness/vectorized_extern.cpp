#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("vectorized_extern", "Externally defined Func f cannot have loop type vectorized (xo)", []() {
        Func f;
        Var x;
        f.define_extern("test", {}, Int(32), {x});
        Var xo;
        f.split(x, xo, x, 8).vectorize(xo);

        f.compile_jit();
    });
}
