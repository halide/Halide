#include "Halide.h"

#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func f;
    Var x, y;

    RDom r(0, 10);
    f(x, y) += r;

    // Sneakily disguising an RVar as a Var by reusing the name should result in
    // an error. Otherwise it can permit schedules that aren't legal.
    Var xo, xi;
    f.update().split(Var(r.x.name()), xo, xi, 8, TailStrategy::RoundUp);

    printf("Success!\n");
    return 0;
}
