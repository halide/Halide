#include "Halide.h"
#include <stdio.h>

using namespace Halide;

// An allocation in registers outside the loops over GPU threads gives every
// thread its own copy of it, so it is only usable if each thread keeps to its
// own part. These are the ways of doing that.

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (!target.has_gpu_feature()) {
        printf("[SKIP] No GPU target enabled.\n");
        return 0;
    }

    {
        // Each thread computes one element and reads back the one it computed.
        Func f("f"), g("g");
        Var x("x"), y("y"), xi("xi"), yi("yi");
        f(x, y) = x + y * 1000;
        g(x, y) = f(x, y) * 2;
        g.gpu_tile(x, y, x, y, xi, yi, 16, 16);
        f.compute_at(g, x).store_in(MemoryType::Register).gpu_threads(x, y);

        Buffer<int> result = g.realize({256, 256}, target);
        for (int y = 0; y < 256; y++) {
            for (int x = 0; x < 256; x++) {
                int correct = (x + y * 1000) * 2;
                if (result(x, y) != correct) {
                    printf("one element per thread: result(%d, %d) = %d instead of %d\n",
                           x, y, result(x, y), correct);
                    return 1;
                }
            }
        }
    }

    {
        // Each thread computes several elements and reads back several of
        // them, which is the shape a thread holding a tile of an accumulator
        // has.
        Func f("f"), g("g");
        Var x("x"), y("y"), xi("xi"), yi("yi"), xii("xii");
        f(x, y) = x + y * 1000;
        g(x, y) = f(x, y) + f(x, y) * 2;
        g.gpu_tile(x, y, x, y, xi, yi, 32, 8)
            .split(xi, xi, xii, 4)
            .unroll(xii);
        f.compute_at(g, xi).store_in(MemoryType::Register).unroll(x).unroll(y);

        Buffer<int> result = g.realize({256, 256}, target);
        for (int y = 0; y < 256; y++) {
            for (int x = 0; x < 256; x++) {
                int correct = (x + y * 1000) * 3;
                if (result(x, y) != correct) {
                    printf("several elements per thread: result(%d, %d) = %d instead of %d\n",
                           x, y, result(x, y), correct);
                    return 1;
                }
            }
        }
    }

    printf("Success!\n");
    return 0;
}
