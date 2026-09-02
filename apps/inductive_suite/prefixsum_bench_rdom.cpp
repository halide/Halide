// Benchmark harness for the "prefix sum along x, then divide by (x+1)"
// pipeline expressed with a plain RDom scan (the non-inductive idiom from
// tutorial/lesson_25_inductive.cpp: prefix_sum(x, y) = undef<int>(), with
// prefix_sum(0, y) and prefix_sum(r, y) update definitions), instead of the
// inductive-function version in prefixsum_bench.cpp.
//
// Unlike the inductive schedule, an RDom forces the entire scan over r to
// complete before output can be computed, so prefix_sum.compute_at(output, y)
// materializes a full row of prefix_sum (not folded to a single register).
// Single-core (no .parallel), same as prefixsum_bench.cpp, so the two are
// directly comparable.
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
    const bool shr = getenv("SHR") != nullptr;  // cheap >>2 consumer (match bench/tbb)

    try {
        Var x("x"), y("y");

        Func input("input"), prefix_sum("prefix_sum"), output("output");
        input(x, y) = (x + y) & 255;  // bounded: no int32 overflow

        RDom r(1, W - 1, "r");
        prefix_sum(x, y) = undef<int>();
        prefix_sum(0, y) = input(0, y);
        prefix_sum(r, y) = prefix_sum(r - 1, y) + input(r, y);

        output(x, y) = shr ? (prefix_sum(x, y) >> 2) : (prefix_sum(x, y) / (x + 1));

        prefix_sum.compute_at(output, y);
        // Parallel over rows (matches oneTBB / the inductive fold); HL_NUM_THREADS=1
        // makes it serial, so one binary covers both the 1-core and multi-core cells.
        output.bound(x, 0, W).bound(y, 0, H).parallel(y);

        Buffer<int> result(W, H);
        output.realize(result);  // warm-up / JIT compile.

        hb::Stats s_bench = hb::bench([&] { output.realize(result); });

        // Measured peak internal-heap footprint (untimed, custom allocator): the
        // actual prefix_sum scratch Halide allocates (one materialized O(W) row per
        // live parallel task -- reused across y -- not the whole O(W*H) trajectory).
        const double meas_bytes = hb::measure_jit_peak(output, [&] { output.realize(result); });

        // Compare against the reference data dumped by prefixsum_bench.cpp.
        int n_mismatch = -1;  // -1 => reference file absent
        FILE *f = fopen(data_path, "rb");
        if (f) {
            int32_t header[2];
            fread(header, sizeof(int32_t), 2, f);
            std::vector<int32_t> in_flat(W * H), halide_out(W * H);
            fread(in_flat.data(), sizeof(int32_t), W * H, f);
            fread(halide_out.data(), sizeof(int32_t), W * H, f);
            fclose(f);

            n_mismatch = 0;
            for (int yy = 0; yy < H; yy++)
                for (int xx = 0; xx < W; xx++)
                    if (result(xx, yy) != halide_out[yy * W + xx]) n_mismatch++;
        }

        // Unfolded footprint = full prefix trajectory O(W*H) (roofline x-axis;
        // matches prefixsum_bench so their rows share one fp/LLC point).
        const double fp_unfold = (double)W * H * 4;
        char note[160];
        snprintf(note, sizeof(note),
                 "Row prefix-sum then %s  W=%d H=%d  (correctness vs inductive dump)  |  unfolded fp/LLC=%.3f",
                 shr ? ">>2" : "/(x+1)", W, H, hb::footprint_over_llc(fp_unfold));
        hb::print_spec_header("prefixsum_bench_rdom", "host", note);
        hb::print_row(shr ? "non-inductive mat (>>2 consumer)" : "non-inductive (RDom, materialize row)", s_bench,
                      (W * (double)H) / (s_bench.min * 1e3), "Mpix/s",
                      meas_bytes, (double)(n_mismatch < 0 ? 0 : n_mismatch), n_mismatch == 0, "", fp_unfold);

        return 0;
    } catch (const Halide::Error &e) {
        fprintf(stderr, "EXCEPTION: %s\n", e.what());
        return 1;
    }
}
