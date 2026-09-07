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

    {
        // Striped across threads rather than tiled: thread t owns elements t,
        // t + 16, t + 32 and t + 48, so it holds four registers.
        Func f("f"), g("g");
        Var x("x"), y("y"), xo("xo"), xi("xi");
        f(x, y) = x + y * 1000;
        g(x, y) = f(x, y);
        g.split(x, xo, xi, 64)
            .split(xi, xi, x, 16)
            .reorder(xi, x, y)
            .unroll(xi)
            .gpu_blocks(xo, y)
            .gpu_threads(x);
        f.compute_at(g, xo)
            .store_in(MemoryType::Register)
            .split(x, xo, xi, 16)
            .unroll(xo)
            .gpu_threads(xi);

        Buffer<int> result = g.realize({256, 4}, target);
        for (int y = 0; y < 4; y++) {
            for (int x = 0; x < 256; x++) {
                if (result(x, y) != x + y * 1000) {
                    printf("striped: result(%d, %d) = %d instead of %d\n",
                           x, y, result(x, y), x + y * 1000);
                    return 1;
                }
            }
        }
    }

    {
        // Two stages that agree about which thread owns which site.
        Func f("f"), g("g");
        Var x("x"), y("y"), xi("xi"), yi("yi");
        f(x, y) = x + y;
        f(x, y) += x + y;
        g(x, y) = f(x, y);
        g.gpu_tile(x, y, x, y, xi, yi, 16, 16);
        f.compute_at(g, x).store_in(MemoryType::Register).gpu_threads(x, y);
        f.update().gpu_threads(x, y);

        Buffer<int> result = g.realize({64, 64}, target);
        for (int y = 0; y < 64; y++) {
            for (int x = 0; x < 64; x++) {
                if (result(x, y) != 2 * (x + y)) {
                    printf("two stages: result(%d, %d) = %d instead of %d\n",
                           x, y, result(x, y), 2 * (x + y));
                    return 1;
                }
            }
        }
    }

    {
        // Each thread owns four elements accessed as one vector, so the vector
        // needs four registers rather than one.
        Func f("f"), g("g");
        Var x("x"), y("y"), xo("xo"), yo("yo"), xi("xi"), yi("yi");
        f(x, y) = x + y * 1000;
        g(x, y) = f(x, y) * 2;
        g.split(x, xo, xi, 32)
            .split(y, yo, yi, 8)
            .split(xi, xi, x, 4)
            .reorder(x, xi, yi, xo, yo)
            .gpu_blocks(xo, yo)
            .gpu_threads(xi, yi)
            .vectorize(x);
        f.compute_at(g, xo)
            .store_in(MemoryType::Register)
            .split(x, xo, xi, 4)
            .gpu_threads(xo, y)
            .vectorize(xi);

        Buffer<int> result = g.realize({256, 64}, target);
        for (int y = 0; y < 64; y++) {
            for (int x = 0; x < 256; x++) {
                int correct = (x + y * 1000) * 2;
                if (result(x, y) != correct) {
                    printf("vectorized: result(%d, %d) = %d instead of %d\n",
                           x, y, result(x, y), correct);
                    return 1;
                }
            }
        }
    }

    {
        // A tile per thread, read back through a wrapper computed inside the
        // loops over threads. The wrapper's own loops start at the thread's
        // part of the allocation, so their bounds speak of the thread rather
        // than being constants, and they still have to bound how far it
        // reaches.
        Func f("f"), g("g");
        Var x("x"), y("y"), xi("xi"), yi("yi"), xii("xii"), yii("yii");
        f(x, y) = x + y * 100;
        g(x, y) = f(x, y) * 2;
        g.tile(x, y, xi, yi, 32, 16)
            .tile(xi, yi, xii, yii, 2, 2)
            .gpu_blocks(x, y)
            .gpu_threads(xi, yi)
            .unroll(xii)
            .unroll(yii);
        f.compute_at(g, x)
            .store_in(MemoryType::Register)
            .tile(x, y, xii, yii, 2, 2)
            .gpu_threads(x, y)
            .unroll(xii)
            .unroll(yii);
        f.in().compute_at(g, xi).unroll(x).unroll(y);

        Buffer<int> result = g.realize({64, 64}, target);
        for (int y = 0; y < 64; y++) {
            for (int x = 0; x < 64; x++) {
                int correct = (x + y * 100) * 2;
                if (result(x, y) != correct) {
                    printf("wrapper: result(%d, %d) = %d instead of %d\n",
                           x, y, result(x, y), correct);
                    return 1;
                }
            }
        }
    }

    printf("Success!\n");
    return 0;
}
