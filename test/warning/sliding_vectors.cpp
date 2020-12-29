#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Func f, g;
    Var x;

    f(x) = x;

    g(x) = f(x - 1) + f(x + 1);

    Var xo, xi, xii;
    g.split(x, xo, xi, 1024)
        .split(xi, xi, xii, 8)
        .vectorize(xii);

    f.store_at(g, xo)
        .compute_at(g, xi)
        .vectorize(x, 8);
    g.realize(1024);

    return 0;
}
