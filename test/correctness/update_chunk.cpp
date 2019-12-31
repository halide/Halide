#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    // This test computes a function within the update step of a reduction

    Func f, g;
    Var x, y, z;
    RDom r(0, 10);

    f(x, y) = x * y;
    g(x, y) = 0;
    g(x, r) = f(r, x) + 1;

    f.compute_at(g, r);
    g.realize(10, 10);

    printf("Success!\n");
    return 0;
}
