#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Var x;
    Param<int> p;

    Func f;
    f(x) = x;
    f.specialize(p == 0).vectorize(x, 8);
    f.specialize_fail("Expected failure");

    p.set(42);  // arbitrary nonzero value
    f.realize(100);

    printf("How did I get here?\n");

    return 0;
}
