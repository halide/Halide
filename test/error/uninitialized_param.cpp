#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    ImageParam image_param(Int(32), 2, "image_param");
    Param<int> scalar_param("scalar_param");

    Var x("x"), y("y");
    Func f("f");

    f(x, y) = image_param(x, y) + scalar_param;

    Buffer<int> b(10, 10);
    image_param.set(b);

    f.realize({10, 10});

    printf("Success!\n");
    return 0;
}
