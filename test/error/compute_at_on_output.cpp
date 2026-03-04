#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func f("f"), g("g");
    Var x("x");

    f(x) = x;
    g(x) = f(x) + 1;

    // g is an output, so it can't be scheduled compute_at another Func.
    g.compute_at(f, x);

    Pipeline({g}).realize({10});

    printf("Success!\n");
    return 0;
}
