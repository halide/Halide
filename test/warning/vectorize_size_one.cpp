#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func f, g;
    Var x, y;
    f(x, y) = x + y;
    g(x, y) = f(x, y);

    // We're vectorizing f, but it's only required over an extent of
    // one, because it's innermost.
    Var xi;
    f.compute_at(g, x).vectorize(x);

    g.realize(16, 16);

    return 0;
}
