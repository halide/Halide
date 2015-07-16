#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func f, g;
    Var x, y;

    f(x) = x;
    g(x) = f(x);

    // f is inlined, so this schedule is bad.
    f.vectorize(x, 4);

    g.realize(10);

    printf("There should have been an error\n");
    return 0;
}
