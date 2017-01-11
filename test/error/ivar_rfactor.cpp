#include <stdio.h>
#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
  Var x;
    IVar imp("imp");

    RDom range(0, 10);
    Func f, g;

    f(x) = 0;
    f(range) += select(imp == 0, 1, f(1));

    Var xi;
    Func fi = f.update(0).rfactor(range, xi);
    f.compute_root();
    g(x, imp) = f(x);

    Buffer<int> im = g.realize(10, 2);

    printf("Should have failed!\n");
    return -1;
}
