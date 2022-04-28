#include "Halide.h"
#include <stdio.h>

using namespace Halide;
using namespace Halide::Internal;

int main(int argc, char **argv) {
    Var x("x"), y("y");
    Func f(Float(32), 2, "f");

    f(x, y) = 0.f;
    f(x, y) = cast<uint8_t>(0);

    f.realize({100, 100});

    printf("Success!\n");
    return 0;
}
