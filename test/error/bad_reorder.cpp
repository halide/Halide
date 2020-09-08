#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Var x, y, xi;

    Func f;

    f(x, y) = x;

    f
        .split(x, x, xi, 8)
        .reorder(x, y, x);

    // Oops, probably meant "xi" rather than x in the reorder call

    printf("Success!\n");
    return 0;
}
