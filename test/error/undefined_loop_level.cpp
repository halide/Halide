#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    LoopLevel undefined;

    Var x;
    Func f, g;
    f(x) = x;
    g(x) = f(x);
    f.compute_at(undefined);
    g.compute_root();

    // Trying to lower/realize with an undefined LoopLevel should be fatal
    Buffer<int> result = g.realize(1);

    printf("I should not have reached here\n");

    return 0;
}
