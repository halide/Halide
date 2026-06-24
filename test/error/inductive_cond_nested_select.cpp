#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func f(Int(32), "f"), g("g");

    Var x("x");

    f(x) = select(x < 0, 0, f(x - 1) + select(x < f(x + 1), 0, 1));
    g(x) = f(x) * 2;

    g.realize({10});

    printf("Success!\n");
    return 0;
}
