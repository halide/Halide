#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func f("f"), g("g");

    Var x("x"), xi("xi"), xo("xo");

    f(x) = select(x < 1, 0, x + f(x - 1));
    f.split(x, xo, xi, 8);
    f.reorder(xo, xi);

    g(x) = f(x) * 2;

    g.realize({10});

    printf("Success!\n");
    return 0;
}
