#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Func f, g;
    Var x;

    Buffer<float> in(100);
    in.for_each_element([&](int x) { in(x) = x * 2.0f; });

    f(x) = in(x) * 3;
    g(x) = f(x) * 2;

    Var xo, xi;
    g.split(x, xo, xi, 8, TailStrategy::GuardWithIf).unroll(xi);
    f.compute_at(g, xo).unroll(x).store_in(MemoryType::Stack);

    Buffer<float> result = g.realize({23});
    for (int i = 0; i < 23; i++) {
        float correct = i * 2 * 3 * 2;
        if (result(i) != correct) {
            printf("result(%d) = %f instead of %f\n", i, result(i), correct);
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
