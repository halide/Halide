#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Func f, g;
    Var x;

    f(x) = x;
    g(x) = f(x)*2;

    Var xo, xi;
    g.split(x, xo, xi, 8, TailStrategy::GuardWithIf);
    f.compute_at(g, xo).unroll(x);

    Buffer<int> result = g.realize(23);
    for (int i = 0; i < 23; i++) {
        if (result(i) != i*2) {
            printf("result(%d) = %d instead of %d\n", i, result(i), i*2);
            return -1;
        }
    }

    return 0;
}
