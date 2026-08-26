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

// The width of the accumulator, how many rows a warp takes, how many warps
// share a block, how many blocks there are, and how many steps the walk takes.
const int W = 8, rows_per_warp = 16, WARPS = 4, BLOCKS = 2, N = 16;
const int Y = rows_per_warp * WARPS * BLOCKS;

// A producer stored above a serial walk, computed within the loop over warps
// inside it, and slid over the walk. Each warp writes and reads only its own
// rows, and only the two steps of the walk that are live.
int slid_over_a_walk_inside_the_warps(const Target &target, MemoryType mem) {
    Func p("p"), acc("acc"), out("out");
    Var x("x"), y("y"), t("t"), yo("yo"), yw("yw"), yi("yi");
    RDom rt(0, N, "rt");

    p(y, t) = cast<float>((y + 1) * (t + 1));

    // Each step needs two consecutive values of p, so p slides over the walk.
    acc(x, y) = 0.f;
    acc(x, y) += (p(y, rt) - p(y, rt - 1)) * cast<float>(x + 1);

    out(x, y) = acc(x, y);

    out.bound(x, 0, W).bound(y, 0, Y).compute_root();
    // A block holds the warps that share the walk, and each of them a stripe
    // of rows, so that the loop over warps below has all of them in it.
    out.split(y, yo, yw, rows_per_warp * WARPS)
        .split(yw, yw, yi, rows_per_warp)
        .gpu_blocks(yo)
        .gpu_threads(yw);

    acc.compute_at(out, yo)
        .split(y, yw, yi, rows_per_warp)
        .gpu_threads(yw)
        .unroll(yi);
    // The walk is outside the loop over warps, so the warps run it together.
    // The rows a warp owns are unrolled, because storage a thread indexes with
    // a loop variable cannot be registers.
    acc.update()
        .split(y, yw, yi, rows_per_warp)
        .reorder(x, yi, yw, rt)
        .gpu_threads(yw)
        .unroll(yi);

    // Stored above the walk - and so above the loop over warps - computed
    // within the warps, and folded down to the two steps that are live.
    //
    // Everything that indexes it is unrolled, here and above, because a
    // register is only a register if which one it is, is known: the rows a
    // warp owns on both sides, and the two live steps it rotates between.
    p.store_at(out, yo)
        .compute_at(acc, yw)
        .store_in(mem)
        .unroll(y)
        .unroll(t);

    Buffer<float> result = out.realize({W, Y}, target);
    result.copy_to_host();

    // p(y, rt) - p(y, rt - 1) is y + 1 at every step, so each row accumulates
    // that times the column, once per step. A warp that read another warp's
    // rows would get a different row's answer.
    for (int y = 0; y < Y; y++) {
        for (int x = 0; x < W; x++) {
            float correct = (float)N * (float)(y + 1) * (float)(x + 1);
            if (result(x, y) != correct) {
                printf("result(%d, %d) = %f instead of %f (memory type %d)\n",
                       x, y, (double)result(x, y), (double)correct, (int)mem);
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

// Finding one store of this thread's that covers a load is not enough: what a
// thread reads is whatever was written to the site last. Here the c == 1 slice
// is written by a stage with no loops over threads of its own, so one thread
// writes it on everyone's behalf, while the c == 0 slice is written by all of
// them. c is a constant in every access, so it cannot be what tells one
// thread's part from another's, and the load of c == 1 must not be excused by
// the store to c == 0.
void one_thread_stands_in_for_all_in_a_slice() {
    Func f("f"), g("g");
    Var x("x"), y("y"), c("c"), xi("xi"), yi("yi");

    f(x, y, c) = undef<int>();
    f(x, y, 0) = x + y * 1000;
    f(x, y, 1) = x + y * 1000 + 7;
    g(x, y) = f(x, y, 0) + f(x, y, 1);

    g.gpu_tile(x, y, x, y, xi, yi, 16, 16);
    f.compute_at(g, x).store_in(MemoryType::Register).bound(c, 0, 2);
    f.update(0).gpu_threads(x, y);
    // f.update(1) is left serial, so one thread runs all of it.

    g.compile_jit(compile_only_target());
}

// The same, with the stage that disagrees running in as many loops over
// threads as the load but mapping them the other way round, so the site a
// thread reads in the c == 1 slice was written by the thread with its
// coordinates transposed.
void stages_disagree_within_a_slice() {
    Func f("f"), g("g");
    Var x("x"), y("y"), c("c"), xi("xi"), yi("yi");

    f(x, y, c) = undef<int>();
    f(x, y, 0) = x + y * 1000;
    f(x, y, 1) = x + y * 1000 + 7;
    g(x, y) = f(x, y, 0) + f(x, y, 1);

    g.gpu_tile(x, y, x, y, xi, yi, 16, 16);
    f.compute_at(g, x).store_in(MemoryType::Register).bound(c, 0, 2);
    f.update(0).gpu_threads(x, y);
    f.update(1).reorder(y, x).gpu_threads(y, x);

    g.compile_jit(compile_only_target());
}

#endif  // HALIDE_WITH_EXCEPTIONS

}  // namespace

int main(int argc, char **argv) {
#if HALIDE_WITH_EXCEPTIONS
    int failures = 0;
    failures += !expect_user_error("reads_another_threads_copy",
                                   "keeps to its own part",
                                   reads_another_threads_copy);
    failures += !expect_user_error("one_thread_stands_in_for_all_in_a_slice",
                                   "keeps to its own part",
                                   one_thread_stands_in_for_all_in_a_slice);
    failures += !expect_user_error("stages_disagree_within_a_slice",
                                   "keeps to its own part",
                                   stages_disagree_within_a_slice);
    if (failures != 0) {
        printf("%d schedule(s) did not produce the expected user error\n", failures);
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
    // Both memory types are private to a thread, and check_gpu_cross_talk
    // checks both, so the schedule is allowed for both.
    for (MemoryType mem : {MemoryType::Register, MemoryType::Stack}) {
        if (slid_over_a_walk_inside_the_warps(target, mem) != 0) {
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
