#include "Halide.h"

#include <cstdio>
#include "benchmark.h"

using namespace Halide;

int main(int argc, char **argv) {
    Var x;

    ImageParam a(Int(32), 1);
    Image<int> b(1), c(1);
    b(0) = 17;
    c(0) = 0;
    a.set(c);

    int expected = 0;
    double t = benchmark(1, 100, [&]() {
        Func f;
        f(x) = a(x) + b(x);
        f.realize(c);
        expected += 17;
        assert(c(0) == expected);
    });

    printf("%g s per jit compilation\n", t);

    printf("Success!\n");
    return 0;
}
