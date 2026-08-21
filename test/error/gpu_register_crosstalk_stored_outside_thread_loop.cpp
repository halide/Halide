#include "Halide.h"
#include <stdio.h>

using namespace Halide;

// The schedule validator lets register storage sit outside a loop over GPU
// threads, because a thread's own registers are not shared with any other
// thread, so such a loop between where it is stored and where it is computed
// is not a race. What that does not establish is that each thread then keeps
// to its own copy, and check_gpu_cross_talk is what establishes it, later in
// lowering. This is a schedule that gets past the first and has to be stopped
// by the second.
//
// See correctness/gpu_register_stored_outside_thread_loop.cpp for the same
// placement without the cross-talk.

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (!target.has_gpu_feature()) {
        printf("[SKIP] No GPU target enabled.\n");
        // An error test has to report an error even when it skips.
        _halide_user_assert(0);
    }

    Func f("f"), g("g");
    Var x("x"), y("y"), xo("xo"), yo("yo"), xi("xi"), yi("yi");

    f(x, y) = x + y * 1000;
    // The second term is the value the neighbouring thread computed.
    g(x, y) = f(x, y) + f(x - 1, y);

    g.gpu_tile(x, y, xo, yo, xi, yi, 16, 16);

    // Stored at the block level, computed within the loop over threads in y,
    // and spread across the threads in x by its own gpu_threads. So no thread
    // holds the whole of f, but each has its own copy, and the value a thread
    // wants from its neighbour is not in the copy it has.
    f.store_at(g, xo)
        .compute_at(g, yi)
        .store_in(MemoryType::Register)
        .gpu_threads(x);

    g.compile_jit(target);

    printf("Success!\n");
    return 0;
}
