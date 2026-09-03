// Benchmark harness for the "prefix sum along x, then divide by (x+1)"
// pipeline expressed with a plain RDom scan (the non-inductive idiom from
// tutorial/lesson_25_inductive.cpp: prefix_sum(x, y) = undef<int>(), with
// prefix_sum(0, y) and prefix_sum(r, y) update definitions), instead of the
// inductive-function version in prefixsum_bench.cpp. Same input: the
// running mean of each row of an int32 image read from memory.
//
// Unlike the inductive schedule, an RDom forces the entire scan over r to
// complete before output can be computed, so prefix_sum.compute_at(output, y)
// materializes a full row of prefix_sum (not folded to a single register).
// The materialized row makes the consumer trivially vectorizable, so
// output's cast, divide and store go 16 wide. Rows run in parallel, like
// prefixsum_bench.cpp; HL_NUM_THREADS=1 makes the run serial.
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

    try {
        Var x("x"), y("y");

        // The input lives in memory, as it does for the baseline. Bounded
        // values, so the running sum never overflows int32.
        Buffer<int32_t> input(W, H, "input");
        for (int yy = 0; yy < H; yy++)
            for (int xx = 0; xx < W; xx++)
                input(xx, yy) = (xx + yy) & 255;

        Func prefix_sum("prefix_sum"), output("output");

        RDom r(1, W - 1, "r");
        prefix_sum(x, y) = undef<int>();
        prefix_sum(0, y) = input(0, y);
        prefix_sum(r, y) = prefix_sum(r - 1, y) + input(r, y);

        // The running mean, in single precision: the sum stays exact in int32.
        output(x, y) = cast<float>(prefix_sum(x, y)) / cast<float>(x + 1);

        prefix_sum.compute_at(output, y);
        output.bound(x, 0, W).bound(y, 0, H).vectorize(x, 16).parallel(y);

        Buffer<float> result(W, H);
        output.realize(result);  // warm-up / JIT compile.

        hb::Stats s_bench = hb::bench([&] { output.realize(result); });

        // Measured peak internal-heap footprint (untimed, custom allocator): the
        // actual prefix_sum scratch Halide allocates (one materialized O(W) row per
        // live parallel task -- reused across y -- not the whole O(W*H) trajectory).
        const double meas_bytes = hb::profiled_peak_bytes(output, result);

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

        // Unfolded footprint = full prefix trajectory O(W*H) (roofline x-axis;
        // matches prefixsum_bench so their rows share one fp/LLC point).
        const double fp_unfold = (double)W * H * 4;
        char note[160];
        snprintf(note, sizeof(note),
                 "Row prefix-sum then /(x+1)  W=%d H=%d  |  unfolded fp/LLC=%.3f",
                 W, H, hb::footprint_over_llc(fp_unfold));
        hb::print_spec_header("prefixsum_bench_rdom", "host", note);
        hb::print_row("non-inductive (RDom, materialize row)", s_bench,
                      (W * (double)H) / (s_bench.min * 1e3), "Mpix/s",
                      meas_bytes, max_rel, max_rel <= 1e-6, "", fp_unfold);

        return 0;
    } catch (const Halide::Error &e) {
        fprintf(stderr, "EXCEPTION: %s\n", e.what());
        return 1;
    }
}
