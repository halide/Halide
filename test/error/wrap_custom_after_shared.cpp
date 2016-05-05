#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func f("f"), g1("g1"), g2("g2"), g3("g3"), g4("g4");
    Var x("x"), y("y");

    f(x) = x;
    g1(x, y) = f(x);
    g2(x, y) = f(x);
    g3(x, y) = f(x);

    // It's not valid to call f.in(g1) after defining a shared wrapper for
    // {g1, g2, g3}
    Func wrapper1 = f.in({g1, g4, g3});
    Func wrapper2 = f.in(g3);
    return 0;
}
