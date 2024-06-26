#include "Halide.h"

#include <map>
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func f("f"), g("g");
    Var x("x"), y("y");

    f(x, y) = x + y;
    g(x, y) = f(0, 0);

    f.compute_root();
    g.prefetch(f, y, x, 8);
    g.print_loop_nest();

    Module m = g.compile_to_module({});

    printf("Success!\n");
    return 0;
}
