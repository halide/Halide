// Benchmark harness for the "prefix sum along x, then divide by (x+1)"
// pipeline from tutorial/lesson_25_inductive.cpp, compared against an
// optimized numpy implementation (bench_numpy.py) on the exact same data.
//
// Uses the fastest schedule from the lesson: prefix_sum is an inductive
// function fused into output's x loop via
// compute_at(x).store_at(y).fold_storage(x, 1) -- a single accumulator
// register per row, no materialized prefix_sum array at all. Single-core
// (no .parallel) to match a straight numpy comparison.
#include "Halide.h"

#include "../support/bench_harness.h"
#include "../support/mem_probe.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace Halide;

int main(int argc, char **argv) {
    int W = argc > 1 ? atoi(argv[1]) : 65536;
    int H = argc > 2 ? atoi(argv[2]) : 32;
    const char *data_path = argc > 3 ? argv[3] : "/tmp/prefixsum_bench_data.bin";
    // SHR=1 swaps the /(x+1) running mean for a cheap >>2 (floor-divide by 4), to
    // isolate the scan/fold cost. Must match across bench/rdom/tbb so the dumped
    // data and the validators agree.
    const bool shr = getenv("SHR") != nullptr;

    try {
        Var x("x"), y("y");

        Func input("input"), prefix_sum(Int(32), "prefix_sum"), output("output");
        input(x, y) = (x + y) & 255;  // bounded: no int32 overflow
        prefix_sum(x, y) = select(x <= 0, input(0, y), likely(prefix_sum(x - 1, y) + input(x, y)));
        output(x, y) = shr ? (prefix_sum(x, y) >> 2) : (prefix_sum(x, y) / (x + 1));

        prefix_sum.compute_at(output, x).store_at(output, y).fold_storage(x, hb::fold_factor(1, W));
        // Parallel over rows (like oneTBB's parallel_for); HL_NUM_THREADS=1 makes
        // it serial, so the one binary covers both the 1-core and multi-core cells.
        output.bound(x, 0, W).bound(y, 0, H).parallel(y);

        Buffer<int> result(W, H);
        output.realize(result);  // warm-up / JIT compile.

        hb::Stats s_bench = hb::bench([&] { output.realize(result); });

        // Measured peak internal-heap footprint (untimed, custom allocator): the
        // actual bytes Halide allocates for prefix_sum's scratch (the result buffer
        // is user-supplied and allocated outside realize, so it is not counted).
        const double meas_bytes = hb::measure_jit_peak(output, [&] { output.realize(result); });
        // Unfolded footprint = the full prefix trajectory O(W*H) folding removes;
        // the roofline x-axis (dimension roles: W = recurrence length, H = batch).
        const double fp_unfold = (double)W * H * 4;
        char note[160];
        snprintf(note, sizeof(note),
                 "Row prefix-sum then %s  W=%d H=%d  (correctness vs numpy, external)  |  unfolded fp/LLC=%.3f",
                 shr ? ">>2" : "/(x+1)", W, H, hb::footprint_over_llc(fp_unfold));
        hb::print_spec_header("prefixsum_bench", "host", note);
        // UNFOLD=1 pins fold_storage(x) to W: same fusion, materializes the full
        // O(W*H) prefix trajectory -> report the unfolded label + footprint.
        const bool unfolded = getenv("UNFOLD") != nullptr;
        const char *label = unfolded ? (shr ? "inductive UNFOLDED (>>2 consumer)" : "inductive UNFOLDED (fold x -> W)") : (shr ? "inductive FOLDED (>>2 consumer)" : "inductive FOLDED (fold x -> 1 accum)");
        hb::print_row(label, s_bench,
                      (W * (double)H) / (s_bench.min * 1e3), "Mpix/s",
                      meas_bytes, 0.0, true, "", fp_unfold);

        // Dump the input array (same formula as `input(x, y) = x + y` above)
        // and the resulting output, so bench_numpy.py can run on identical
        // data and we can check for exact agreement.
        {
            std::vector<int32_t> in_flat(W * H);
            for (int yy = 0; yy < H; yy++)
                for (int xx = 0; xx < W; xx++)
                    in_flat[yy * W + xx] = (xx + yy) & 255;

            std::vector<int32_t> out_flat(W * H);
            for (int yy = 0; yy < H; yy++)
                for (int xx = 0; xx < W; xx++)
                    out_flat[yy * W + xx] = result(xx, yy);

            FILE *f = fopen(data_path, "wb");
            int32_t header[2] = {W, H};
            fwrite(header, sizeof(int32_t), 2, f);
            fwrite(in_flat.data(), sizeof(int32_t), W * H, f);
            fwrite(out_flat.data(), sizeof(int32_t), W * H, f);
            fclose(f);
        }

        return 0;
    } catch (const Halide::Error &e) {
        fprintf(stderr, "EXCEPTION: %s\n", e.what());
        return 1;
    }
}
