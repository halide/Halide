#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Func f;
    Var x;
    f(x) = x;
    f(x) += 1;
    Var xo, xi, xio, xii;
    // Would redundantly redo some +=1, and create incorrect output.
    f.compute_root();
    f.update().split(x, xo, xi, 8).split(xi, xio, xii, 9, TailStrategy::RoundUp);

    Func g;
    g(x) = f(x);
    g.realize(10);

    return 0;
}
