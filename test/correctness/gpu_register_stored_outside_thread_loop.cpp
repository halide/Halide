// Register memory is private to a GPU thread, so a loop over threads between
// where a Func is stored and where it is computed is not a race: each thread
// gets its own copy rather than sharing one. Storing at the block level and
// computing within the threads is therefore allowed.
//
// What makes such a schedule safe is that each thread only ever touches the
// part of its own copy that it wrote, and that is check_gpu_cross_talk's
// question rather than the schedule validator's. So the two halves below are a
// pair: a placement that is fine and used to be refused up front, and one that
// is not fine, gets past the validator, and has to be caught by the cross-talk
// check instead.

#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

namespace {

// Each thread computes and reads only its own (x, y), so no thread depends on
// a value another one was responsible for.
int keeps_to_its_own_copy(const Target &target) {
    Func f("f"), g("g");
    Var x("x"), y("y"), xo("xo"), yo("yo"), xi("xi"), yi("yi");

    f(x, y) = x + y * 1000;
    g(x, y) = f(x, y) * 2;

    g.gpu_tile(x, y, xo, yo, xi, yi, 16, 16);
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
    return 0;
}

#if HALIDE_WITH_EXCEPTIONS

// Reaching the error only needs compiling, so this half runs without a device.
Target compile_only_target() {
    return get_host_target()
        .with_feature(Target::CUDA)
        .with_feature(Target::CUDACapability80);
}

// Stored at the block level, computed within the loop over threads in y, and
// spread across the threads in x by its own gpu_threads. No thread holds the
// whole of f, and the value each one wants from its neighbour is not in the
// copy it has.
void reads_another_threads_copy() {
    Func f("f"), g("g");
    Var x("x"), y("y"), xo("xo"), yo("yo"), xi("xi"), yi("yi");

    f(x, y) = x + y * 1000;
    g(x, y) = f(x, y) + f(x - 1, y);

    g.gpu_tile(x, y, xo, yo, xi, yi, 16, 16);
    f.store_at(g, xo)
        .compute_at(g, yi)
        .store_in(MemoryType::Register)
        .gpu_threads(x);

    g.compile_jit(compile_only_target());
}

#endif  // HALIDE_WITH_EXCEPTIONS

}  // namespace

int main(int argc, char **argv) {
#if HALIDE_WITH_EXCEPTIONS
    if (!expect_user_error("reads_another_threads_copy",
                           "keeps to its own part",
                           reads_another_threads_copy)) {
        return 1;
    }
#else
    printf("[SKIP] Halide was compiled without exceptions, so the schedule "
           "that must be rejected is not exercised.\n");
#endif

    Target target = get_jit_target_from_environment();
    if (!target.has_gpu_feature()) {
        printf("[SKIP] No GPU target enabled, so the schedule that must be "
               "accepted is not exercised.\n");
        return 0;
    }
    if (keeps_to_its_own_copy(target) != 0) {
        return 1;
    }

    printf("Success!\n");
    return 0;
}
