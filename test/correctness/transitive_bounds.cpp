#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func f, g;
    Var x;
    f(x) = x;
    g(x) = f(x) + 1;

    g.bound(x, 0, 4);

    // Should be ok to unroll x because it's bounded by a constant in its only consumer
    f.compute_root().unroll(x);

    g.realize(4);

    printf("Success!\n");
    return 0;
}
