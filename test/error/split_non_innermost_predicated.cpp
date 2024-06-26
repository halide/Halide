#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Func f;
    Var x;
    f(x) = x;
    Var xo, xi, xio, xii;
    // We don't support predicated splits that aren't the innermost loop.
    f.compute_root().split(x, xo, xi, 8, TailStrategy::PredicateStores).split(xi, xio, xii, 9);

    Func g;
    g(x) = f(x);
    g.realize({10});

    printf("Success!\n");
    return 0;
}
