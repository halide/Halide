#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Func f("f"), g("g");
    Var x, y, c("c");

    f(x, y, c) = x + y + c;

    g(x, y, c) = f(x, y, c) + f(x, y, 3);

    Var xi, yi, ci;
    g.compute_root().tile(x, y, xi, yi, 32, 32);
    f.compute_at(g, x).bound(c, 0, 4).unroll(c);

    g.realize(1024, 1024, 4);

    // f's loop over channels has two bounds. The first outer one
    // comes from its relationship with g - it needs to satisfy
    // however many channels of g are required. The second inner one
    // is the constant range given by the bound directive. These two
    // bounds appear as a shadowed .min/.max variable. We want to
    // ensure simplify_correlated_differences respects the inner const
    // bound instead of substituting in the outer one. The schedule
    // above is a little silly in that it overcomputes f, but it's
    // designed to be just complex enough to tempt
    // simplify_correlated_differences into trying to substitute in
    // the outer bound to cancel the c.

    // It's sufficient to check that we compiled.
    printf("Success!\n");
    return 0;
}
