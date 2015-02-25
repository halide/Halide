#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Var x, y, c;
    Func f("f"), g;

    f(x, y, c) = 1;
    g(x, y, c) = f(x, y, c);

    // Store the intermediate with color channels innermost
    f.compute_root().reorder_storage(c, x, y);

    Image<int> im = g.realize(10, 10, 3);

    // The strides should be invisible to the front-end, so the only
    // way to check this is to read the output with HL_DEBUG_CODEGEN=1

    printf("Success!\n");
    return 0;
}
