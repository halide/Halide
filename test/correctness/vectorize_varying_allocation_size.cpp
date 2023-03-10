#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Func f, g;
    Var x, xo, xi;

    f(x) = x;
    g(x) = f(x) + f(x * x - 20);

    g.split(x, xo, xi, 4).vectorize(xi);
    f.compute_at(g, xi);

    // The region required of f is [min(x, x*x-20), max(x, x*x-20)],
    // which varies nastily with the var being vectorized.

    Buffer<int> out = g.realize({100});

    for (int i = 0; i < 4; i++) {
        int correct = i + i * i - 20;
        if (out(i) != correct) {
            printf("out(%d) = %d instead of %d\n",
                   i, out(i), correct);
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
