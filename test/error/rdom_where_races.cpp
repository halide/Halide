// https://github.com/halide/Halide/issues/6808
#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func f;
    Var x;

    RDom r(0, 10);
    f(x) = 1;
    r.where(f(0) == 1);
    f(r) = 2;

    f.update().parallel(r);

    printf("Success!\n");
    return 0;
}
