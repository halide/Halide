#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Func f;
    Var x, xo, xi, xio, xii;
    f(x) = x;
    f.split(x, xo, xi, 2, TailStrategy::Auto)
        .split(xi, xio, xii, 4, TailStrategy::PredicateLoads)
        .reorder(xo, xio, xii);

    printf("Success!\n");
    return 0;
}
