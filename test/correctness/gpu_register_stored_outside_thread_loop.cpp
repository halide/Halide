#include "Halide.h"
#include <stdio.h>

using namespace Halide;

// Register memory is private to a GPU thread, so a loop over threads between
// where it is stored and where it is computed is not a race: each thread gets
// its own copy rather than sharing one. Storing at the block level and
// computing within the threads is therefore allowed, and what makes it safe is
// that each thread only ever touches the part of its own copy that it wrote.
// That is check_gpu_cross_talk's business, not the schedule validator's - see
// error/gpu_register_crosstalk_stored_outside_thread_loop.cpp for the other
// side of it.

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (!target.has_gpu_feature()) {
        printf("[SKIP] No GPU target enabled.\n");
        return 0;
    }

    Func f("f"), g("g");
    Var x("x"), y("y"), xo("xo"), yo("yo"), xi("xi"), yi("yi");

    f(x, y) = x + y * 1000;
    g(x, y) = f(x, y) * 2;

    g.gpu_tile(x, y, xo, yo, xi, yi, 16, 16);

    // Stored outside the loops over threads, computed inside them. Each thread
    // writes and reads only its own (x, y).
    f.store_at(g, xo)
        .compute_at(g, xi)
        .store_in(MemoryType::Register);

    Buffer<int> result = g.realize({64, 64}, target);
    result.copy_to_host();

    for (int y = 0; y < result.height(); y++) {
        for (int x = 0; x < result.width(); x++) {
            int correct = (x + y * 1000) * 2;
            if (result(x, y) != correct) {
                printf("result(%d, %d) = %d instead of %d\n",
                       x, y, result(x, y), correct);
                return 1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
