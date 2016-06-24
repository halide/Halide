#include "Halide.h"
#include <cstdio>
#include "benchmark.h"

using namespace Halide;

int main(int argc, char **argv) {
    const int size = 50;

    Var x("x"), y("y");
    Func f ("f"), g("g");

    g(x, y) = x + y;
    g.compute_root();

    RDom r(0, size, 0, size);
    r.where(r.x + r.y < size);

    f(x, y) = 10;
    f(r.x, r.y) += g(r.x, r.y) * 2;
    f.update(0).vectorize(r.x, 8);

    Image<int> im = f.realize(size, size);

    /*const int iterations = 50;
    double t = benchmark(1, iterations, [&]() {
        f.realize(size, size);
    });

    printf("Halide: %fms\n\n", t * 1e3);*/

    printf("Success!\n");
    return 0;
}
