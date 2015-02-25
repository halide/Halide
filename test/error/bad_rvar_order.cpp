#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    RDom r1(0, 10, 0, 10);

    Func f("f");
    Var x, y;
    f(x, y) = x + y;
    f(r1.x, r1.y) += f(r1.y, r1.x);

    // It's not permitted to change the relative ordering of reduction
    // domain variables when it could change the meaning.
    f.update().reorder(r1.y, r1.x);

    f.realize(10, 10);

    printf("Success!\n");
    return 0;
}
