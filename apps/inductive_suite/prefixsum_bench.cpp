// Benchmark harness for the "prefix sum along x, then divide by (x+1)"
// pipeline from tutorial/lesson_25_inductive.cpp: the running mean of each
// row of an int32 image, read from memory.
//
// prefix_sum is an inductive function fused into output's x loop and
// folded, so no prefix_sum array is ever materialized. Two schedules:
//
//   default   output's x is split into groups of GROUP (default 64, a power
//             of two) with the inner part vectorized 16 wide, so the cast,
//             divide and store go 16 wide. prefix_sum is computed per group
//             at the outer loop (the scan itself is serial) into a ring of
//             2*GROUP per row (fold_storage): a group and the value before it.
//             The scan is unrolled by 8 so the running sum is carried in a
//             register through the unrolled body and reloaded from the ring
//             once per 8 elements, rather than round-tripping through the
//             ring at every element.
//   SCALAR=1  prefix_sum computed per output element, folded to a single
//             two-slot window per row (fold_storage(x, 2)); everything scalar.
//
// Rows run in parallel (like oneTBB's parallel_for); HL_NUM_THREADS=1 makes
// the run serial, so one binary covers both the 1-core and the all-cores
// cells. UNFOLD=1 pins the fold to W, materializing the full prefix
// trajectory with the fusion unchanged.
#include "Halide.h"

#include "../support/bench_harness.h"
#include "../support/jit_support.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace Halide;

int main(int argc, char **argv) {
    int W = argc > 1 ? atoi(argv[1]) : 65536;
    int H = argc > 2 ? atoi(argv[2]) : 32;
    const bool scalar = getenv("SCALAR") != nullptr;
    const bool unfolded = getenv("UNFOLD") != nullptr;
    const int vec = 16;
    const int group = hb::env_int("GROUP", 4 * vec);
    if (!scalar && (group % vec != 0 || W % group != 0)) {
        fprintf(stderr, "W must be a multiple of GROUP=%d, itself a multiple of %d (or set SCALAR=1)\n", group, vec);
        return 1;
    }

    try {
        Var x("x"), y("y"), xo("xo"), xi("xi");

        // The input lives in memory, as it does for the baseline. Bounded
        // values, so the running sum never overflows int32.
        Buffer<int32_t> input(W, H, "input");
        for (int yy = 0; yy < H; yy++)
            for (int xx = 0; xx < W; xx++)
                input(xx, yy) = (xx + yy) & 255;

        Func prefix_sum(Int(32), "prefix_sum"), output("output");
        prefix_sum(x, y) = select(x <= 0, input(0, y), likely(prefix_sum(x - 1, y) + input(x, y)));
        // The running mean, in single precision: the sum stays exact in int32.
        output(x, y) = cast<float>(prefix_sum(x, y)) / cast<float>(x + 1);

        output.bound(x, 0, W).bound(y, 0, H).parallel(y);
        int fold;
        if (scalar) {
            fold = hb::fold_factor(2, W);
            prefix_sum.compute_at(output, x).store_at(output, y).fold_storage(x, fold);
        } else {
            fold = hb::fold_factor(2 * group, W);
            output.split(x, xo, xi, group).vectorize(xi);
            prefix_sum.compute_at(output, xo).store_at(output, y).fold_storage(x, fold).unroll(x, 8);
        }

        Buffer<float> result(W, H);
        output.realize(result);  // warm-up / JIT compile.

        hb::Stats s_bench = hb::bench([&] { output.realize(result); });

        // Measured peak internal-heap footprint (untimed, custom allocator): the
        // actual bytes Halide allocates for prefix_sum's scratch (the result buffer
        // is user-supplied and allocated outside realize, so it is not counted).
        const double meas_bytes = hb::profiled_peak_bytes(output, result);
        // Unfolded footprint = the full prefix trajectory O(W*H) folding removes;
        // the roofline x-axis (dimension roles: W = recurrence length, H = batch).
        const double fp_unfold = (double)W * H * 4;

        // Correctness against a serial reference: the same running int32 sum
        // and float divide. The divide may go by a reciprocal in either form,
        // so the comparison is relative, to 1e-6.
        double max_rel = 0;
        for (int yy = 0; yy < H; yy++) {
            int32_t run = 0;
            for (int xx = 0; xx < W; xx++) {
                run += input(xx, yy);
                float expect = (float)run / (float)(xx + 1);
                double rel = std::abs((double)result(xx, yy) - expect) / std::abs((double)expect);
                if (rel > max_rel) max_rel = rel;
            }
        }

        char note[160];
        snprintf(note, sizeof(note),
                 "Row prefix-sum then /(x+1)  W=%d H=%d  |  unfolded fp/LLC=%.3f",
                 W, H, hb::footprint_over_llc(fp_unfold));
        hb::print_spec_header("prefixsum_bench", "host", note);
        char label[96];
        if (unfolded) {
            snprintf(label, sizeof(label), "inductive UNFOLDED (fold x -> W%s)", scalar ? "" : ", vec 16");
        } else if (scalar) {
            snprintf(label, sizeof(label), "inductive FOLDED (fold x -> 2)");
        } else {
            snprintf(label, sizeof(label), "inductive FOLDED (fold x -> %d, group %d, vec %d)", fold, group, vec);
        }
        hb::print_row(label, s_bench,
                      (W * (double)H) / (s_bench.min * 1e3), "Mpix/s",
                      meas_bytes, max_rel, max_rel <= 1e-6, "", fp_unfold);

        return 0;
    } catch (const Halide::Error &e) {
        fprintf(stderr, "EXCEPTION: %s\n", e.what());
        return 1;
    }
}
