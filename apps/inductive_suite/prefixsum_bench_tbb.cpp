// oneTBB baselines for the prefix-sum-then-average pipeline from
// tutorial/lesson_25_inductive.cpp, on the same int32 image the Halide forms
// read from memory:
//
//   input(x, y)      = (x + y) & 255
//   prefix_sum(x, y) = sum_{i<=x} input(i, y)
//   output(x, y)     = float(prefix_sum(x, y)) / float(x + 1)
//
// Two variants, both fusing the running sum and the division so no prefix
// array is ever materialized:
//
//   parallel_for rows   tbb::parallel_for over rows, each row a serial scan:
//                       running int32 sum, float divide, store. The obvious
//                       best code when there are at least as many rows as
//                       threads.
//   parallel_scan       tbb::parallel_for over rows with tbb::parallel_scan
//                       along each row, which also parallelizes within a row
//                       (a two-pass chunked scan) when rows are fewer than
//                       threads.
//
// Honors HL_NUM_THREADS for its thread count, the same env the Halide forms
// use, so the 1-core and all-cores cells are measured the same way.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

#include "../support/bench_harness.h"
#include <oneapi/tbb.h>

using namespace oneapi;

namespace {

// Rows in parallel, each row a serial fused scan.
void fused_prefix_mean_rows(const int32_t *inp, float *out, int W, int H) {
    tbb::parallel_for(tbb::blocked_range<int>(0, H), [&](const tbb::blocked_range<int> &yr) {
        for (int y = yr.begin(); y < yr.end(); y++) {
            const int32_t *row_in = inp + (size_t)y * W;
            float *row_out = out + (size_t)y * W;
            int32_t run = 0;
            for (int x = 0; x < W; x++) {
                run += row_in[x];
                row_out[x] = (float)run / (float)(x + 1);
            }
        }
    });
}

// Rows in parallel, each row a tbb::parallel_scan (the lambda-based overload).
// The scan lambda computes a running int32 sum and, on the final pass, fuses
// in the division by (x + 1) as a side effect; the return value only carries
// the running sum forward.
void fused_prefix_mean_scan(const int32_t *inp, float *out, int W, int H) {
    tbb::parallel_for(tbb::blocked_range<int>(0, H), [&](const tbb::blocked_range<int> &yr) {
        for (int y = yr.begin(); y < yr.end(); y++) {
            const int32_t *row_in = inp + (size_t)y * W;
            float *row_out = out + (size_t)y * W;
            tbb::parallel_scan(
                tbb::blocked_range<int>(0, W), (int32_t)0,
                [&](const tbb::blocked_range<int> &r, int32_t sum, bool is_final_scan) -> int32_t {
                    // The branch on is_final_scan is hoisted out of the
                    // element loop so the hot loop is a bare accumulate + store.
                    int32_t temp = sum;
                    if (!is_final_scan) {
                        for (int i = r.begin(); i < r.end(); i++)
                            temp += row_in[i];
                    } else {
                        for (int i = r.begin(); i < r.end(); i++) {
                            temp += row_in[i];
                            row_out[i] = (float)temp / (float)(i + 1);
                        }
                    }
                    return temp;
                },
                [](int32_t a, int32_t b) -> int32_t { return (int32_t)(a + b); });
        }
    });
}

// Largest relative error against a serial reference: the same running int32
// sum and float divide. Compiled with fast-math, the division may go by a
// reciprocal, so the comparison is relative, to 1e-6.
double max_rel_error(const int32_t *inp, const float *out, int W, int H) {
    double max_rel = 0;
    for (int y = 0; y < H; y++) {
        int32_t run = 0;
        for (int x = 0; x < W; x++) {
            run += inp[(size_t)y * W + x];
            float expect = (float)run / (float)(x + 1);
            double rel = std::abs((double)out[(size_t)y * W + x] - expect) / std::abs((double)expect);
            if (rel > max_rel) max_rel = rel;
        }
    }
    return max_rel;
}

}  // namespace

int main(int argc, char **argv) {
    int W = argc > 1 ? atoi(argv[1]) : 65536;
    int H = argc > 2 ? atoi(argv[2]) : 32;

    // The same input as the Halide forms, filled outside the timed region.
    // Bounded values, so the running sum never overflows int32.
    std::vector<int32_t> inp((size_t)W * H);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            inp[(size_t)y * W + x] = (x + y) & 255;
    std::vector<float> out((size_t)W * H);

    int nth = 0;
    if (const char *e = getenv("HL_NUM_THREADS")) nth = atoi(e);
    std::unique_ptr<tbb::global_control> gc;
    if (nth > 0)
        gc.reset(new tbb::global_control(tbb::global_control::max_allowed_parallelism, nth));

    char note[128];
    snprintf(note, sizeof(note),
             "Row prefix-sum then /(x+1)  W=%d H=%d  (3rd-party: oneTBB)", W, H);
    hb::print_spec_header("prefixsum_bench_tbb", "host", note);

    // Both variants fuse the scan and the division, so no prefix array is
    // materialized (footprint O(1) per thread).
    fused_prefix_mean_rows(inp.data(), out.data(), W, H);  // warm-up.
    hb::Stats s_rows = hb::bench([&] { fused_prefix_mean_rows(inp.data(), out.data(), W, H); });
    double err_rows = max_rel_error(inp.data(), out.data(), W, H);
    hb::print_row("oneTBB parallel_for rows (serial scan)", s_rows,
                  (W * (double)H) / (s_rows.min * 1e3), "Mpix/s",
                  0.0, err_rows, err_rows <= 1e-6);

    std::fill(out.begin(), out.end(), 0.0f);
    fused_prefix_mean_scan(inp.data(), out.data(), W, H);  // warm-up.
    hb::Stats s_scan = hb::bench([&] { fused_prefix_mean_scan(inp.data(), out.data(), W, H); });
    double err_scan = max_rel_error(inp.data(), out.data(), W, H);
    hb::print_row("oneTBB parallel_scan (fused)", s_scan,
                  (W * (double)H) / (s_scan.min * 1e3), "Mpix/s",
                  0.0, err_scan, err_scan <= 1e-6);

    return 0;
}
