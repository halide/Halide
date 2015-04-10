#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func f, g;
    Var x, y;
    f(x, y) = x + y;
    g(x, y) = f(x, y) + f(x + 1, y);

    // Nested vectorization should cause a warning.
    Var xi;
    g.split(x, x, xi, 8).vectorize(xi);
    f.compute_at(g, xi).vectorize(x);

    g.realize(16, 16);

    return 0;
}
