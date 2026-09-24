#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Var x("x"), xo("xo"), xi("xi"), xoo("xoo"), xoi("xoi");
    Func f("f"), g("g");

    g(x) = x;
    f(x) = g(x) + 1;

    f.compute_root()
        .split(x, xo, xi, 8, TailStrategy::ShiftInwards)
        .split(xo, xoo, xoi, 2, TailStrategy::PredicateLoads);

    f.realize({20});

    printf("Success!\n");
    return 0;
}
