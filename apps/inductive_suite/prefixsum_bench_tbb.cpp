// Benchmark an oneTBB tbb::parallel_scan implementation of the
// prefix-sum-then-average pipeline from tutorial/lesson_25_inductive.cpp,
// on the exact same data dumped by prefixsum_bench.cpp.
//
// Pipeline: input(x, y) = x + y
//           prefix_sum(x, y) = sum_{i<=x} input(i, y)
//           output(x, y) = prefix_sum(x, y) // (x + 1)
//
// Unlike bench_numpy.py's numpy/numba comparisons (deliberately pinned to
// one thread to match Halide's single-core schedule), this benchmark uses
// oneTBB's parallel_scan to fuse the running sum and the division into a
// single multi-threaded pass -- a genuinely optimized, parallel third-party
// baseline, rather than a single-core one.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

#include "../support/bench_harness.h"
#include <oneapi/tbb.h>

using namespace oneapi;

namespace {

// Uses oneTBB's lambda-based parallel_scan overload (backed internally by
// lambda_scan_body, so no hand-written Body class/reverse_join/assign/split
// constructor is needed here). The scan lambda computes a running int32 sum
// (matching Halide's Int(32) prefix_sum, including its silent overflow
// behavior for large W) and, on the final pass, fuses in the division by
// (x + 1) as a side effect, so no separate prefix_sum array is ever
// materialized; the return value only carries the running sum forward.
void fused_prefix_mean_tbb(const int32_t *inp, int32_t *out, int W, int H, bool shr) {
    tbb::parallel_for(tbb::blocked_range<int>(0, H), [&](const tbb::blocked_range<int> &yr) {
        for (int y = yr.begin(); y < yr.end(); y++) {
            const int32_t *row_in = inp + (size_t)y * W;
            int32_t *row_out = out + (size_t)y * W;
            tbb::parallel_scan(
                tbb::blocked_range<int>(0, W), (int32_t)0,
                [&](const tbb::blocked_range<int> &r, int32_t sum, bool is_final_scan) -> int32_t {
                    // Branches (is_final_scan, shr) are hoisted OUT of the element
                    // loop so the hot loop is a bare accumulate + store -- no
                    // per-element branch. Inputs are bounded (&255) so temp never
                    // overflows and stays >= 0, making `/` and `>>` plain floors.
                    int32_t temp = sum;
                    if (!is_final_scan) {
                        for (int i = r.begin(); i < r.end(); i++)
                            temp += row_in[i];
                    } else if (shr) {
                        for (int i = r.begin(); i < r.end(); i++) {
                            temp += row_in[i];
                            row_out[i] = temp >> 2;
                        }
                    } else {
                        for (int i = r.begin(); i < r.end(); i++) {
                            temp += row_in[i];
                            row_out[i] = temp / (i + 1);
                        }
                    }
                    return temp;
                },
                [](int32_t a, int32_t b) -> int32_t { return (int32_t)(a + b); });
        }
    });
}

}  // namespace

int main(int argc, char **argv) {
    const char *data_path = argc > 1 ? argv[1] : "/tmp/prefixsum_bench_data.bin";

    FILE *f = fopen(data_path, "rb");
    if (!f) {
        fprintf(stderr, "Could not open %s (run prefixsum_bench first)\n", data_path);
        return 1;
    }
    int32_t header[2];
    if (fread(header, sizeof(int32_t), 2, f) != 2) {
        fprintf(stderr, "Bad header in %s\n", data_path);
        return 1;
    }
    int W = header[0], H = header[1];

    std::vector<int32_t> inp((size_t)W * H), halide_out((size_t)W * H), out((size_t)W * H);
    if (fread(inp.data(), sizeof(int32_t), (size_t)W * H, f) != (size_t)W * H ||
        fread(halide_out.data(), sizeof(int32_t), (size_t)W * H, f) != (size_t)W * H) {
        fprintf(stderr, "Bad data in %s\n", data_path);
        return 1;
    }
    fclose(f);

    const bool shr = getenv("SHR") != nullptr;  // cheap >>2 consumer (match bench/rdom)

    // Honor HL_NUM_THREADS so oneTBB can be measured single-core (=1, fair vs the
    // single-core inductive/non-inductive benches) or multi-core (unset/=cores,
    // fair vs the parallel two-stage fold), controlled by the same env everything
    // else uses.
    int nth = 0;
    if (const char *e = getenv("HL_NUM_THREADS")) nth = atoi(e);
    std::unique_ptr<tbb::global_control> gc;
    if (nth > 0)
        gc.reset(new tbb::global_control(tbb::global_control::max_allowed_parallelism, nth));

    fused_prefix_mean_tbb(inp.data(), out.data(), W, H, shr);  // warm-up.

    hb::Stats s_bench = hb::bench([&] { fused_prefix_mean_tbb(inp.data(), out.data(), W, H, shr); });

    size_t n_mismatch = 0;
    for (size_t i = 0; i < out.size(); i++) {
        if (out[i] != halide_out[i]) n_mismatch++;
    }

    // Third-party parallel baseline: oneTBB parallel_scan fuses the scan and the
    // division, so no prefix array is materialized (footprint ~O(1) per thread).
    char note[128];
    snprintf(note, sizeof(note),
             "Row prefix-sum then %s  W=%d H=%d  (3rd-party: oneTBB parallel_scan)",
             shr ? ">>2" : "/(x+1)", W, H);
    hb::print_spec_header("prefixsum_bench_tbb", "host", note);
    hb::print_row(shr ? "oneTBB parallel_scan (>>2)" : "oneTBB parallel_scan (fused)", s_bench,
                  (W * (double)H) / (s_bench.min * 1e3), "Mpix/s",
                  0.0, (double)n_mismatch, n_mismatch == 0);

    return 0;
}
