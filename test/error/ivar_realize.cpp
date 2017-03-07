#include <stdio.h>
#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Var x;
    IVar imp("imp");

    Func f("f");
    f(x, imp) = 0;

    Func g("g");
    g(x) = f(x, imp);

    // Cannot realize a Func that has implicit vars
    Buffer<int> im = g.realize(10);

    printf("Should have failed!\n");
    return -1;
}
