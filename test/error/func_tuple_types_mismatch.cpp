#include "Halide.h"
#include <stdio.h>

using namespace Halide;
using namespace Halide::Internal;

int main(int argc, char **argv) {
    Var x("x"), y("y");
    Func f({UInt(8), Float(64)}, 2, "f");

    f(x, y) = {cast<int>(0), cast<float>(0)};

    f.realize({100, 100});

    printf("Success!\n");
    return 0;
}
