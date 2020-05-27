#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func f("f"), g("g");
    Var x("x"), y("y");

    f(x) = x;
    g(x) = f(x);
    Func wrapper = f.in(g);
    wrapper(x) += 1;

    printf("Success!\n");
    return 0;
}
