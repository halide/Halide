#include "Halide.h"
#include <stdio.h>

using namespace Halide;
using namespace Halide::Internal;

int main(int argc, char **argv) {
    Var x("x"), y("y");
    Func f(Float(32), 1, "f");

    f(x, y) = cast<int>(0);

    f.realize({100, 100});

    printf("Success!\n");
    return 0;
}
