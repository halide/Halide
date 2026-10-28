#include "Halide.h"
#include "expect_user_error.h"

#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    // The region of lut that is used depends on the size of the output, so
    // it can't be precomputed.
    return error_test("precompute_unbounded", "Use Func::bound()", []() {
        Var x("x"), y("y");
        Func lut("lut"), g("g");
        lut(x, y) = x + y;
        g(x, y) = lut(x % 8, y);
        lut.precompute();
        g.compile_to_module({});
    });
}
