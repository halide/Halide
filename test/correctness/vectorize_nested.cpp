#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func f("f");
    Var x("x"), y("y"), xi("xi"), yi("yi");

    f(x, y) = x + y;

    f.compute_root()
        .tile(x, y, x, y, xi, yi, 4, 4, TailStrategy::GuardWithIf)
        .vectorize(xi, 4)
        .vectorize(yi, 4);

    Buffer<int> result = f.realize(24, 28);

    printf("Success\n");
    return 0;
}
