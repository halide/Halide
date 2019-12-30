#include "Halide.h"

#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func f;
    Var x;
    f(x) = x % 0;

    f.realize(10);

    printf("Success!\n");
    return 0;
}
