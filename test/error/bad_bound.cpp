#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func f("f");
    Var x("x"), y("y");

    f(x) = 0;
    f.bound(y, 0, 10);

    return 0;
}
