#include "Halide.h"
#include "halide_benchmark.h"

using namespace Halide;

int main(int argc, char **argv) {
    Param<int> p;

    const char *names[3] = {"heap", "pseudostack", "stack"};

    double t[3];
    for (int i = 0; i < 3; i++) {
        Func f("f"), g("g");
        Var x("x");

        f(x) = x;
        g(x) = f(x);

        Var xo, xi;
        g.split(x, xo, xi, p, TailStrategy::GuardWithIf);

        f.compute_at(g, xo);
        if (i != 0) {
            f.store_in(MemoryType::Stack);
        }
        if (i == 2) {
            f.bound_extent(x, p);
            g.specialize(p == 8);
        }

        p.set(8);

        Buffer<int> out(1024 * 1024);
        t[i] = Halide::Tools::benchmark([&] {g.realize(out);});
        printf("Time using %s: %f\n", names[i], t[i]);
    }

    if (t[0] < t[1]) {
        printf("Heap allocation was faster than pseudostack!\n");
        return -1;
    }

    return 0;
}
