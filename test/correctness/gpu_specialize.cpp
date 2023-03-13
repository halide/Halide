#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    if (!get_jit_target_from_environment().has_gpu_feature()) {
        printf("[SKIP] No GPU target enabled.\n");
        return 0;
    }

    {
        Func f("f"), g("g"), h("h");
        Var x("x"), y("y");

        Param<bool> use_gpu;

        f(x, y) = x + y;
        g(x, y) = f(x - 1, y + 1) + f(x + 1, y - 1) + x;
        h(x, y) = g(x + 1, y - 1) + g(x - 1, y + 1) + y;

        // Specialize is a little tricky for producer-consumer pairs: the
        // compute_at must be the same in either case, which means you
        // must have a matching var name in either case.

        // Compute h in tiles either on the cpu or gpu
        Var xo("xo"), yo("yo"), xi("xi"), yi("yi"), t("t");
        h.compute_root().specialize(use_gpu).gpu_tile(x, y, xi, yi, 4, 4);
        h.tile(x, y, xo, yo, xi, yi, 8, 8).fuse(xo, yo, t).parallel(t);

        // Peel off a size-1 loop from blockidx to make a scheduling point
        // that matches the cpu case. We need to mark it as serial,
        // because by default when you split up a parallel loop both the
        // inside and outside are parallel.
        h.specialize(use_gpu).split(x, x, t, 1).serial(t);

        // Because t exists in both version of h, we can compute g at it.
        g.compute_at(h, t);

        // If we're on the gpu, we should map g's x and y to thread ids
        g.specialize(use_gpu).gpu_threads(x, y);

        // We want f compute_at g, x, so do the same trick to g;
        g.specialize(use_gpu).split(x, x, xi, 1).serial(xi);
        g.rename(x, xi);

        f.compute_at(g, xi);

        use_gpu.set(get_jit_target_from_environment().has_gpu_feature());
        Buffer<int> out1 = h.realize({1024, 1024});
        use_gpu.set(false);
        Buffer<int> out2 = h.realize({1024, 1024});

        for (int y = 0; y < out1.height(); y++) {
            for (int x = 0; x < out1.width(); x++) {
                int correct = 6 * x + 5 * y;
                if (out1(x, y) != correct) {
                    printf("out1(%d, %d) = %d instead of %d\n",
                           x, y, out1(x, y), correct);
                    return 1;
                }
                if (out2(x, y) != correct) {
                    printf("out2(%d, %d) = %d instead of %d\n",
                           x, y, out2(x, y), correct);
                    return 1;
                }
            }
        }
    }

    {
        Func f("f"), g("g"), h("h");
        Var x("x"), y("y");

        Param<bool> p;

        f(x, y) = x + y;
        g(x, y) = f(x, y) + x;

        Var xo("xo"), yo("yo"), xi("xi"), yi("yi");
        f.specialize(p).tile(x, y, xi, yi, 4, 4).gpu_threads(x, y);
        f.tile(x, y, xo, yo, xi, yi, 8, 8).gpu_threads(xo, yo);

        f.compute_at(g, x);
        g.tile(x, y, xi, yi, 2, 2).gpu_blocks(x, y);

        p.set(true);
        Buffer<int> out = g.realize({32, 32});

        for (int y = 0; y < out.height(); y++) {
            for (int x = 0; x < out.width(); x++) {
                int correct = 2 * x + y;
                if (out(x, y) != correct) {
                    printf("out(%d, %d) = %d instead of %d\n",
                           x, y, out(x, y), correct);
                    return 1;
                }
            }
        }
    }

    printf("Success!\n");
    return 0;
}
