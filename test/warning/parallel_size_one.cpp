#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func f;
    Var x, y;

    f(x, y) = x + y;
    f.bound(y, 0, 1);
    f.parallel(y);

    f.realize(10, 1);

    return 0;
}
