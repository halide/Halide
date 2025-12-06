#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func f("f"), g("g");

    Var x("x"), y("y");

    f(x, y) = select(x < 1, 0, x + f(y - 1, x - 1));

    g(x, y) = f(x, y) * 2;

    g.realize({10, 10});

    printf("Success!\n");
    return 0;
}
