#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {

    Var x("x");
    Func f("f"), g("g");

    f(x) = 2 * x;
    g(x) = f(x) / 2;

    Var xo, xi;
    f.compute_at(g, x).split(x, xo, xi, 16).vectorize(xi, 8).unroll(xi);
    g.compute_root().vectorize(x, 16);

    Buffer<int> r = g.realize({16});
    for (int i = 0; i < 16; i++) {
        if (r(i) != i) {
            std::cout << "Error at " << i << ": " << r(i) << std::endl;
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
