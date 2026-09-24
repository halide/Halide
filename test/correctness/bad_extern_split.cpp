#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("bad_extern_split", "Externally defined Func f cannot have extern loop", []() {
        Func f;
        Var x;
        f.define_extern("test", {}, Int(32), {x});
        Var xo;
        f.split(x, xo, x, 8).reorder(xo, x);

        f.compile_jit();
    });
}
