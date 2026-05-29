#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func f("f"), g("g");

    Var x("x"), y("y");

    f(x, y) = select(x < 2 || y < 2 || y > 6, 0, f(x - 1, y + 1)) + select(x < 2 || x > 6 || y < 2, 0, f(x + 1, y - 1));
    g(x, y) = f(x, y) * 2;

    g.realize({10, 10});

    printf("Success!\n");
    return 0;
}
