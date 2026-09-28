#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("unbounded_output", "unbounded way in dimension ", []() {
        Func f;
        Var x, y;

        ImageParam in(Float(32), 2);
        ImageParam x_coord(Int(32), 2);
        ImageParam y_coord(Int(32), 2);

        f(x, y) = 0.0f;
        RDom r(0, 100, 0, 100);
        f(x_coord(r.x, r.y), y_coord(r.x, r.y)) += in(r.x, r.y);

        f.compile_jit();
    });
}
