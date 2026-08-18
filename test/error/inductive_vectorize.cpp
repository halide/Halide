#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func f("f"), g("g");

    Var x("x");

    f(x) = select(x < 1, 0, x + f(x - 1));
    f.vectorize(x, 8);

    g(x) = f(x) * 2;

    g.realize({10});

    printf("Success!\n");
    return 0;
}
