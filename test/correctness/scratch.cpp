#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Func f, g;
    Var x;

    f(x) = x;

    g(x) = f(x) + f(x - 1);

    if (getenv("V1")) {
        // Version 1: Unwanted modulo by 2
        f.store_root().compute_at(g, x);
    } else {
        // Version 2: Attempt to remove modulo by 2 by unrolling, but doesn't work,
        // because f now slides over either the outer loop or the inner loop,
        // instead of the original meaning of 'x', which is now distributed across
        // multiple loops.

        Var xo, xi;
        g.align_bounds(x, 2).split(x, xo, xi, 2).unroll(xi);
        f.store_root().compute_at(g, xi).slide(g, x).unroll(x);
    }

    g.compile_jit();

    return 0;
}
