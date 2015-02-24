#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {

    Func f0, f1, f2, g("g"), out("out");
    Var x("x"), y("y");

    f0(x, y) = 0;
    f1(x, y) = 1;
    f2(x, y) = 2;


    g(x, y) = 0;
    g(x, x) = g(x, x-1) + g(x, x+1) + f0(x, x);
    g(y, y) = g(y-1, y) + g(y+1, y) + f1(y, y);
    g(x, x) = g(x, x-1) + g(x, x+1) + f2(x, x);
    out(x, y) = g(x-1, y-1) + g(x+1, y+1);

    g.store_root().compute_at(out, x);
    f0.store_at(out, x).compute_at(g, x);
    f1.store_at(out, x).compute_at(g, y);
    f2.store_at(out, x).compute_at(g, x);

    out.realize(10, 10);

    // We just want this to not segfault.

    printf("Success!\n");

    return 0;
}
