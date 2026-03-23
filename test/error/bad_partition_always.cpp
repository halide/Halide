#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func f("f");
    Var x("x");

    f(x) = 0;
    f.partition(x, Partition::Always);

    f.realize({10});

    printf("Success!\n");
    return 0;
}
