#include "Halide.h"
#include <stdio.h>

using namespace Halide;
using namespace Halide::Internal;

int main(int argc, char **argv) {
    Var x("x"), y("y");
    Func f(Float(32), 1, "f");
    f.define_extern("test", {}, Float(32), {x, y});
    f.realize({100, 100});
    printf("Success!\n");
    return 0;
}
