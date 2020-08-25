#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Var x, y, xi;

    Func f;

    f(x, y) = x;

    f.reorder_storage(x, y, x);

    printf("Success!\n");
    return 0;
}
