#include "Halide.h"
#include "expect_user_error.h"

#include <map>
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("bad_prefetch", "Prefetch 'from' variable 'x' could not be found in an active loop. (Are the 'at' and 'from' variables swapped?)", []() {
        Func f("f"), g("g");
        Var x("x"), y("y");

        f(x, y) = x + y;
        g(x, y) = f(0, 0);

        f.compute_root();
        g.prefetch(f, y, x, 8);
        g.print_loop_nest();

        Module m = g.compile_to_module({});
    });
}
