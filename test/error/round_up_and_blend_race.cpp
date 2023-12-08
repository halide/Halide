#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {

    Func f;
    Var x;

    f(x) = 0;
    f(x) += 4;

    // This schedule should be forbidden, because it causes a race condition.
    Var xo, xi;
    f.update()
        .split(x, xo, xi, 8, TailStrategy::RoundUp)
        .vectorize(xi, 16, TailStrategy::RoundUpAndBlend)  // Access beyond the end of each slice
        .parallel(xo);

    printf("Success!\n");
    return 0;
}
