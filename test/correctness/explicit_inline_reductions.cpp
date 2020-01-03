#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func f("f");
    Var x("x"), y("y");
    RDom r1(0, 10, "r1"), r2(0, 10, "r2"), r3(0, 10, "r3");

    f(x, y) = product(sum(r1, r1 + r3) + sum(r2, r2 * 2 + r3));
    f(r1, y) += product(r3, sum(r2, r1 + r2 + r3));

    Buffer<int> result = f.realize(10, 10);

    return 0;
}
