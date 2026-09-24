#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Var x("x"), y("y");

    // Only one-element Tuples can be used as Exprs.
    Expr e = Tuple(x, y);

    printf("Success!\n");
    return 0;
}
