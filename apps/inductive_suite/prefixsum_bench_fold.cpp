// Prefix-sum-then-average as a scan PARALLELIZED OVER THE TIME AXIS -- the fair
// counterpart to oneTBB's tbb::parallel_scan, which parallelizes a single long
// scan across its index. A plain serial scan over t has no parallelism along
// time; oneTBB gets it via a two-pass (up-sweep / down-sweep) chunked scan, so
// we express the same structure in Halide with inductive functions.
//
// Layout: t = k*L + j is the time axis (W = C*L), split into C chunks of length
// L; s indexes S independent spatial lanes. The scan over t is done in three
// stages, and the parallel axis is the chunk index k (i.e. we parallelize over
// time):
//
//   input(t, s)       = t + s
//   local(j, k, s)    = sum_{i<=j} input(k*L+i, s)        (inductive over j; per-chunk local scan)
//   ctot(k, s)        = local(L-1, k, s)                  (chunk total)
//   carry(k, s)       = sum_{q<k} ctot(q, s)              (inductive over k; exclusive chunk prefix)
//   full(j, k, s)     = carry(k, s) + local(j, k, s)      (add inter-chunk carry)
//   output(j, k, s)   = full(j, k, s) / (k*L + j + 1)
//
// local is inductive over j and folded (fold_storage(j,1)); the up-sweep
// (local + ctot) and the down-sweep (full/output) both run .parallel(k) -- the
// time dimension. carry is the short serial O(C) scan over chunk totals, the
// only inherently sequential part (exactly oneTBB's serial reduction step).
//
// Dumps the same {W,H,input,output} data file as prefixsum_bench.cpp (flattened
// back to t-major per lane) so prefixsum_bench_tbb.cpp validates on identical data.
#include "../support/bench_harness.h"
#include "Halide.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace Halide;

int main(int argc, char **argv) {
    int W = argc > 1 ? atoi(argv[1]) : 1048576;  // time length
    int S = argc > 2 ? atoi(argv[2]) : 32;       // independent spatial lanes
    int L = argc > 3 ? atoi(argv[3]) : 4096;     // chunk length
    const char *data_path = argc > 4 ? argv[4] : "/tmp/prefixsum_bench_data.bin";
    // Post-scan consumer: default is the running mean /(t+1) (a hardware integer
    // divide by a runtime-variable divisor -- expensive, and it dominates the
    // measurement). SHR=1 selects a cheap >>2 (arithmetic shift = floor-divide by
    // 4) instead, which isolates the scan/fold cost from the divide.
    const bool shr = getenv("SHR") != nullptr;

    if (W % L != 0) {  // keep the chunking exact; round W down to a multiple of L.
        W = (W / L) * L;
        if (W == 0) {
            fprintf(stderr, "W must be >= L\n");
            return 1;
        }
    }
    int C = W / L;  // number of time chunks (the parallel axis).

    try {
        Var j("j"), k("k"), s("s");

        Func input("input"), ctot("ctot"), carry(Int(32), "carry"),
            local(Int(32), "local"), output("output");
        input(j, k, s) = ((k * L + j) + s) & 255;  // bounded: no int32 overflow

        // Up-sweep: each chunk's total, a plain reduction (parallel over chunks).
        RDom r(0, L, "r");
        ctot(k, s) = sum(input(r, k, s));

        // Serial O(C) exclusive prefix over chunk totals -- the only inherently
        // sequential stage (matches oneTBB's serial reduction between passes).
        carry(k, s) = select(k <= 0, 0,
                             likely(carry(k - 1, s) + ctot(k - 1, s)));

        // Down-sweep: per-chunk local prefix scan (inductive over j), add the
        // inter-chunk carry, divide. local is private to output so it folds.
        local(j, k, s) = select(j <= 0, input(0, k, s),
                                likely(local(j - 1, k, s) + input(j, k, s)));
        Expr scanned = carry(k, s) + local(j, k, s);
        output(j, k, s) = shr ? (scanned >> 2) : (scanned / (k * L + j + 1));

        // Parallelize over the time dimension (chunk axis k), placed OUTERMOST so
        // the whole grid is one parallel region (one fork/join), with the lanes
        // serial inside -- not a fresh parallel region per lane. local folds to a
        // single accumulator per lane inside each chunk; carry stays serial.
        ctot.compute_root().reorder(s, k).parallel(k);
        carry.compute_root();
        local.compute_at(output, j).store_at(output, k).fold_storage(j, hb::fold_factor(1, L));
        output.bound(j, 0, L).bound(k, 0, C).bound(s, 0, S).reorder(j, s, k).parallel(k);  // k outermost parallel; s serial inside

        Buffer<int> result(L, C, S);
        output.realize(result);  // warm-up / JIT compile.

        hb::Stats s_bench = hb::bench([&] { output.realize(result); });

        // Footprint: local folds to O(C*S) accumulators + ctot/carry O(C*S);
        // no O(W*S) prefix array materialized.
        const double bytes_fold = (double)C * S * 3 * 4;
        // Unfolded footprint = the O(W*S) prefix trajectory the 2-stage fold
        // avoids (roofline x-axis; W = recurrence length, S = parallel lanes).
        const double fp_unfold = (double)W * S * 4;
        char note[200];
        snprintf(note, sizeof(note),
                 "Time prefix-sum then %s  W=%d S=%d L=%d C=%d  (2-stage scan, parallel over time chunks)  |  unfolded fp/LLC=%.3f",
                 shr ? ">>2 (cheap consumer)" : "/(t+1) (running mean)", W, S, L, C,
                 hb::footprint_over_llc(fp_unfold));
        hb::print_spec_header("prefixsum_bench_fold", "host", note);

        // Correctness vs a straight serial per-lane prefix sum. Inputs are bounded
        // (&255) so the running sum never overflows int32 and stays non-negative --
        // both `/` and `>>` are then plain floor divides, matching Halide exactly.
        size_t n_mismatch = 0;
        for (int ss = 0; ss < S; ss++) {
            int32_t run = 0;
            for (int tt = 0; tt < W; tt++) {
                run += (tt + ss) & 255;
                int32_t expect = shr ? (run >> 2) : (run / (tt + 1));
                if (result(tt % L, tt / L, ss) != expect) n_mismatch++;
            }
        }
        // UNFOLD=1 pins the per-chunk local fold to L: same fusion, materializes
        // the O(W*S) prefix trajectory -> report the unfolded label + footprint.
        const bool unfolded = getenv("UNFOLD") != nullptr;
        const char *label = unfolded ? (shr ? "2-stage UNFOLDED (>>2 consumer)" : "inductive 2-stage UNFOLDED (fold j -> L)") : (shr ? "2-stage FOLDED (>>2 consumer)" : "inductive 2-stage FOLDED (parallel time)");
        hb::print_row(label, s_bench,
                      (W * (double)S) / (s_bench.min * 1e3), "Mpix/s",
                      unfolded ? fp_unfold : bytes_fold, (double)n_mismatch, n_mismatch == 0, "", fp_unfold);

        // Dump input+output for the oneTBB validator ONLY in the default /(t+1)
        // mode -- the >>2 output would not match oneTBB's running-mean reference.
        if (!shr) {
            std::vector<int32_t> in_flat((size_t)W * S), out_flat((size_t)W * S);
            for (int ss = 0; ss < S; ss++)
                for (int tt = 0; tt < W; tt++) {
                    in_flat[(size_t)ss * W + tt] = (tt + ss) & 255;
                    out_flat[(size_t)ss * W + tt] = result(tt % L, tt / L, ss);
                }
            FILE *f = fopen(data_path, "wb");
            int32_t header[2] = {W, S};
            fwrite(header, sizeof(int32_t), 2, f);
            fwrite(in_flat.data(), sizeof(int32_t), (size_t)W * S, f);
            fwrite(out_flat.data(), sizeof(int32_t), (size_t)W * S, f);
            fclose(f);
        }

        return 0;
    } catch (const Halide::Error &e) {
        fprintf(stderr, "EXCEPTION: %s\n", e.what());
        return 1;
    }
}
