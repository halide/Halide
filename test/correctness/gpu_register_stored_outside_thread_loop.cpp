// Register memory is private to a GPU thread, so a loop over threads between
// where a Func is stored and where it is computed is not a race: each thread
// gets its own copy rather than sharing one.
//
// The point of such a placement is not the loop over threads, which nothing
// can slide or fold over. It is that a serial loop can sit in there too. A
// group of warps cooperates on a walk over a serial reduction, and a producer
// feeding it slides over that walk while staying computed inside the warps, so
// its values never have to leave registers to cross between them. That needs
// the storage above the walk, which puts it above the loop over warps as well.
//
// What makes it safe is that each warp only ever touches the rows of its own
// copy that it wrote, and that is check_gpu_cross_talk's question rather than
// the schedule validator's. So the two halves below are a pair: a placement
// that is fine and used to be refused up front, and one that is not fine, gets
// past the validator, and has to be caught by the cross-talk check instead.

#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

namespace {

// The width of the accumulator, how many rows there are, how many warps share
// them, and how many steps the walk takes.
const int W = 8, Y = 64, WARPS = 4, N = 16;
const int rows_per_warp = Y / WARPS;

// A producer stored above a serial walk, computed within the loop over warps
// inside it, and slid over the walk. Each warp writes and reads only its own
// rows, and only the two steps of the walk that are live.
int slid_over_a_walk_inside_the_warps(const Target &target) {
    Func p("p"), acc("acc"), out("out");
    Var x("x"), y("y"), t("t"), yo("yo"), yw("yw"), yi("yi");
    RDom rt(0, N, "rt");

    p(y, t) = cast<float>((y + 1) * (t + 1));

    // Each step needs two consecutive values of p, so p slides over the walk.
    acc(x, y) = 0.f;
    acc(x, y) += (p(y, rt) - p(y, rt - 1)) * cast<float>(x + 1);

    out(x, y) = acc(x, y);

    out.bound(x, 0, W).bound(y, 0, Y).compute_root();
    out.split(y, yo, yw, rows_per_warp).gpu_blocks(yo).gpu_threads(yw);

    acc.compute_at(out, yo)
        .split(y, yw, yi, rows_per_warp)
        .gpu_threads(yw);
    // The walk is outside the loop over warps, so the warps run it together.
    acc.update()
        .split(y, yw, yi, rows_per_warp)
        .reorder(x, yi, yw, rt)
        .gpu_threads(yw);

    // Stored above the walk - and so above the loop over warps - computed
    // within the warps, and folded down to the two steps that are live.
    p.store_at(out, yo)
        .compute_at(acc, yw)
        .store_in(MemoryType::Register);

    Buffer<float> result = out.realize({W, Y}, target);
    result.copy_to_host();

    // p(y, rt) - p(y, rt - 1) is y + 1 at every step, so each row accumulates
    // that times the column, once per step. A warp that read another warp's
    // rows would get a different row's answer.
    for (int y = 0; y < Y; y++) {
        for (int x = 0; x < W; x++) {
            float correct = (float)N * (float)(y + 1) * (float)(x + 1);
            if (result(x, y) != correct) {
                printf("result(%d, %d) = %f instead of %f\n",
                       x, y, (double)result(x, y), (double)correct);
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
    if (slid_over_a_walk_inside_the_warps(target) != 0) {
        return 1;
    }

    printf("Success!\n");
    return 0;
}
