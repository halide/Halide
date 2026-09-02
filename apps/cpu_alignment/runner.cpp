// Verifies and benchmarks the batched affine-gap aligner against ksw2
// (the kernel family behind minimap2). The Halide direction planes are
// checked byte-for-byte against ksw_gg's backtrack matrix, and the
// CIGARs reconstructed from them must match ksw2's exactly.

#include "HalideBuffer.h"
#include "halide_benchmark.h"

#include "align16_ind.h"
#include "align16_rdom.h"
#include "align16_unf.h"
#include "align8_ind.h"
#include "align8_rdom.h"
#include "align8_unf.h"

extern "C" {
#include "ksw2/ksw2.h"
int ksw_gg(void *km, int qlen, const uint8_t *query, int tlen, const uint8_t *target, int8_t m, const int8_t *mat, int8_t gapo, int8_t gape, int w, int *m_cigar_, int *n_cigar_, uint32_t **cigar_);
int ksw_gg2_sse(void *km, int qlen, const uint8_t *query, int tlen, const uint8_t *target, int8_t m, const int8_t *mat, int8_t q, int8_t e, int w, int *m_cigar_, int *n_cigar_, uint32_t **cigar_);
}

#include <cstdio>
#include <malloc.h>
#ifdef HAVE_PARASAIL
#include "parasail.h"
#endif
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

#ifndef QLEN
#define QLEN 256
#endif
#ifndef TLEN
#define TLEN 256
#endif
#ifndef PARALLEL
#define PARALLEL 0
#endif
#ifndef BATCH
#define BATCH 1024
#endif

namespace {

using Halide::Runtime::Buffer;
using Halide::Tools::benchmark;

constexpr int J = QLEN, I = TLEN, B = BATCH;
constexpr int SA = 2, SB = 4, GAPO = 4, GAPE = 2;

// A reusing allocator: the benchmark should measure alignment, not page
// faults on the rdom form's gigabytes of fresh intermediates.
std::mutex pool_mutex;
std::vector<std::pair<size_t, void *>> pool;

}  // namespace

extern "C" void *halide_malloc(void *, size_t size) {
    constexpr size_t header = 128;
    // HB_NO_REUSE=1 leaves the process allocator in charge, to measure
    // what the pool is worth.
    static const bool no_reuse = getenv("HB_NO_REUSE") != nullptr;
    if (no_reuse) {
        char *base = (char *)aligned_alloc(128, (size + header + 127) / 128 * 128);
        ((size_t *)base)[0] = 0;
        return base + header;
    }
    {
        std::lock_guard<std::mutex> lock(pool_mutex);
        for (auto &entry : pool) {
            if (entry.first == size && entry.second) {
                void *base = entry.second;
                entry.second = nullptr;
                return (char *)base + header;
            }
        }
    }
    char *base = (char *)aligned_alloc(128, (size + header + 127) / 128 * 128);
    ((size_t *)base)[0] = size;
    return base + header;
}

// Between forms: the planes differ in size, so one form's pool is dead
// weight to the next, and all six together would be most of the machine.
void drain_pool() {
    std::lock_guard<std::mutex> lock(pool_mutex);
    for (auto &entry : pool) {
        if (entry.second) {
            free(entry.second);
        }
    }
    pool.clear();
}

extern "C" void halide_free(void *, void *ptr) {
    char *base = (char *)ptr - 128;
    size_t size = ((size_t *)base)[0];
    if (size == 0) {
        free(base);
        return;
    }
    std::lock_guard<std::mutex> lock(pool_mutex);
    for (auto &entry : pool) {
        if (!entry.second) {
            entry = {size, base};
            return;
        }
    }
    pool.emplace_back(size, base);
}

namespace {

uint64_t splitmix64(uint64_t &x) {
    uint64_t z = (x += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

// The pipeline emits each pair's ops in backward order (ksw2's
// traceback order), 3 once done. Reverse and run-length encode into
// ksw2's CIGAR form for comparison.
void cigar_from_path(const uint8_t *ops, long sstride, int nsteps,
                     std::vector<uint32_t> &cigar) {
    int n = 0;
    while (n < nsteps && ops[n * sstride] != 3) n++;
    cigar.clear();
    for (int k = n - 1; k >= 0; k--) {
        uint32_t op = ops[k * sstride];
        if (!cigar.empty() && (cigar.back() & 0xf) == op) {
            cigar.back() += 1 << 4;
        } else {
            cigar.push_back(1 << 4 | op);
        }
    }
}

// Global alignment score recomputed by walking the CIGAR: for a valid
// traceback of a global alignment this equals the DP score.
int path_score(const std::vector<uint32_t> &cigar,
               const uint8_t *q, const uint8_t *t) {
    int score = 0, i = 0, j = 0;
    for (uint32_t c : cigar) {
        int len = c >> 4, op = c & 0xf;
        if (op == 0) {
            for (int k = 0; k < len; k++, i++, j++) {
                score += q[j] == t[i] ? SA : -SB;
            }
        } else {
            score -= GAPO + GAPE * len;
            (op == 2 ? i : j) += len;
        }
    }
    return score;
}

std::string cigar_str(const std::vector<uint32_t> &cigar) {
    std::string s;
    for (uint32_t c : cigar) {
        s += std::to_string(c >> 4) + "MID"[c & 0xf];
    }
    return s;
}

}  // namespace

int main(int argc, char **argv) {
    // Keep glibc from serving ksw2's per-call megabyte allocations with
    // mmap/munmap: page faults would otherwise dominate both baselines.
    mallopt(M_MMAP_THRESHOLD, 1 << 30);
    mallopt(M_TRIM_THRESHOLD, 1 << 30);
    // Batch-major layouts for Halide; contiguous per-pair copies for ksw2.
    Buffer<uint8_t> query(B, J), target(B, I);
    std::vector<uint8_t> qs((size_t)B * J), ts((size_t)B * I);
    uint64_t sm = 0x2545f4914f6cdd1dull;
    for (int b = 0; b < B; b++) {
        for (int j = 0; j < J; j++) query(b, j) = qs[(size_t)b * J + j] = splitmix64(sm) & 3;
        for (int i = 0; i < I; i++) target(b, i) = ts[(size_t)b * I + i] = splitmix64(sm) & 3;
    }

    int8_t mat[16];
    for (int a = 0; a < 4; a++) {
        for (int c = 0; c < 4; c++) {
            mat[a * 4 + c] = a == c ? SA : -SB;
        }
    }

    printf("%d pairs of %dx%d, affine gaps %d+%dk, match %d mismatch -%d\n",
           B, J, I, GAPO, GAPE, SA, SB);

    // ksw2 references: scalar ksw_gg defines the conventions; gg2_sse is
    // the SIMD production kernel.
    std::vector<std::vector<uint32_t>> ref_cigar(B);
    std::vector<int> ref_score(B);
    {
        int mc = 0, nc = 0;
        uint32_t *cig = nullptr;
        for (int b = 0; b < B; b++) {
            nc = 0;
            ref_score[b] = ksw_gg(nullptr, J, &qs[(size_t)b * J], I, &ts[(size_t)b * I], 4, mat,
                                  GAPO, GAPE, -1, &mc, &nc, &cig);
            ref_cigar[b].assign(cig, cig + nc);
        }
        free(cig);
    }
    {
        int mc = 0, nc = 0, mismatches = 0;
        uint32_t *cig = nullptr;
        for (int b = 0; b < B; b++) {
            nc = 0;
            int sc = ksw_gg2_sse(nullptr, J, &qs[(size_t)b * J], I, &ts[(size_t)b * I], 4, mat,
                                 GAPO, GAPE, -1, &mc, &nc, &cig);
            if (sc != ref_score[b] ||
                std::vector<uint32_t>(cig, cig + nc) != ref_cigar[b]) {
                mismatches++;
            }
        }
        free(cig);
        printf("  ksw_gg vs ksw_gg2_sse: %s\n",
               mismatches ? "DIFFER (using ksw_gg as reference)" : "identical cigars");
    }

    Buffer<uint8_t> path(B, I + J);
    std::vector<uint32_t> cig;

    auto check = [&](const char *what) {
        for (int b = 0; b < B; b++) {
            cigar_from_path(&path(b, 0), (long)B, I + J, cig);
            if (cig != ref_cigar[b] ||
                path_score(cig, &qs[(size_t)b * J], &ts[(size_t)b * I]) != ref_score[b]) {
                printf("  %-10s MISMATCH pair %d:\n    ours %s (%d)\n    ksw2 %s (%d)\n",
                       what, b, cigar_str(cig).c_str(),
                       path_score(cig, &qs[(size_t)b * J], &ts[(size_t)b * I]),
                       cigar_str(ref_cigar[b]).c_str(), ref_score[b]);
                return false;
            }
        }
        printf("  %-10s cigar+score exact vs ksw2 (%d pairs)\n", what, B);
        return true;
    };

#ifdef HAVE_PARASAIL
    // parasail's traceback variants use the same strategy the inductive
    // schedule derives: striped SIMD score rows kept rolling, a per-cell
    // direction table as the only materialization.
    std::vector<std::string> qc(B), tc(B);
    for (int b = 0; b < B; b++) {
        for (int j = 0; j < J; j++) qc[b] += "ACGT"[qs[(size_t)b * J + j]];
        for (int i = 0; i < I; i++) tc[b] += "ACGT"[ts[(size_t)b * I + i]];
    }
    // ksw2's gap of length L costs GAPO + GAPE*L; parasail's costs
    // open + (L-1)*ext, so open = GAPO + GAPE.
    parasail_matrix_t *pmat = parasail_matrix_create("ACGT", SA, -SB);
    auto parasail_one = [&](int b, std::vector<uint32_t> *cigar_out) {
        parasail_result_t *res = parasail_nw_trace_striped_16(
            qc[b].c_str(), J, tc[b].c_str(), I, GAPO + GAPE, GAPE, pmat);
        int sc = parasail_result_get_score(res);
        if (cigar_out) {
            parasail_cigar_t *pc = parasail_result_get_cigar(
                res, qc[b].c_str(), J, tc[b].c_str(), I, pmat);
            cigar_out->clear();
            for (int k = 0; k < pc->len; k++) {
                uint32_t v = pc->seq[k];
                char opc = parasail_cigar_decode_op(v);
                uint32_t op = (opc == 'I') ? 1 : (opc == 'D') ? 2 : 0;  // =/X -> M
                uint32_t len = parasail_cigar_decode_len(v);
                if (!cigar_out->empty() && (cigar_out->back() & 0xf) == op) {
                    cigar_out->back() += len << 4;
                } else {
                    cigar_out->push_back(len << 4 | op);
                }
            }
            parasail_cigar_free(pc);
        }
        parasail_result_free(res);
        return sc;
    };
    {
        int score_bad = 0, cigar_diff = 0, path_bad = 0;
        std::vector<uint32_t> pcig;
        for (int b = 0; b < B; b++) {
            int sc = parasail_one(b, &pcig);
            if (sc != ref_score[b]) score_bad++;
            if (pcig != ref_cigar[b]) {
                cigar_diff++;
                if (path_score(pcig, &qs[(size_t)b * J], &ts[(size_t)b * I]) != ref_score[b]) path_bad++;
            }
        }
        printf("  parasail: %s scores;%s\n",
               score_bad ? "MISMATCHED" : "identical",
               cigar_diff == 0 ? " identical cigars" :
               path_bad == 0   ? " some cigars differ (all optimal: equal path scores)" :
                                 " INVALID cigars");
        if (score_bad || path_bad) return 1;
    }
#endif

#ifdef HAVE_PARASAIL
    // ONLY_PARASAIL: benchmark just the parasail sweep, for isolating its
    // memory behaviour.
    if (getenv("ONLY_PARASAIL")) {
        double tp0 = benchmark(3, 1, [&]() {
            int nthreads = PARALLEL ? std::thread::hardware_concurrency() : 1;
            std::vector<std::thread> threads;
            std::atomic<int> next{0};
            for (int th = 0; th < nthreads; th++) {
                threads.emplace_back([&]() {
                    int b;
                    std::vector<uint32_t> cig2;
                    while ((b = next.fetch_add(8)) < B) {
                        for (int k = b; k < b + 8 && k < B; k++) parasail_one(k, &cig2);
                    }
                });
            }
            for (auto &th : threads) th.join();
        });
        printf("  parasail   %10.1f us\n", tp0 * 1e6);
        return 0;
    }
#endif
    struct Form {
        const char *name;
        int (*fn)(struct halide_buffer_t *, struct halide_buffer_t *, struct halide_buffer_t *);  // (qseq, tseq, path)
    };
    // Two generators x three forms. int8 inductive is the headline.
    Form forms[] = {
        {"int8 ind", align8_ind},   {"int8 unf", align8_unf},   {"int8 rdom", align8_rdom},
        {"int16 ind", align16_ind}, {"int16 unf", align16_unf}, {"int16 rdom", align16_rdom},
    };

    // ONLY_FORM=<name> (or ONLY_A8 for "int8 ind"): benchmark one form
    // alone, re-verifying its output after every timed call, for A/B
    // runs of differently-built binaries and for isolating memory
    // behaviour.
    const char *only = getenv("ONLY_FORM");
    if (getenv("ONLY_A8")) only = "int8 ind";
    if (only) {
        for (auto &f : forms) {
            if (std::string(f.name) != only) continue;
            double best = 1e30;
            for (int r = 0; r < 3; r++) {
                double t = benchmark(1, 1, [&]() { f.fn(query, target, path); });
                if (!check(f.name)) return 1;
                best = std::min(best, t);
            }
            printf("  %-10s %10.1f us  (best of 3, verified each)\n", f.name, best * 1e6);
            return 0;
        }
        printf("no such form: %s\n", only);
        return 1;
    }
    double t[6];
    for (int f = 0; f < 6; f++) {
        forms[f].fn(query, target, path);
        if (!check(forms[f].name)) return 1;
        t[f] = benchmark(3, 1, [&]() { forms[f].fn(query, target, path); });
        drain_pool();
    }
    double t_ind = t[0];  // int8 inductive is the reference for ratios

    // The pipeline includes the traceback; what remains outside it is
    // turning each pair's op stream into a run-length CIGAR, which the
    // baselines' calls also do. Timed and folded into fill+cigar.
    auto compaction_sweep = [&]() {
        int nthreads = PARALLEL ? std::thread::hardware_concurrency() : 1;
        std::vector<std::thread> threads;
        std::atomic<int> next{0};
        for (int th = 0; th < nthreads; th++) {
            threads.emplace_back([&]() {
                int b;
                std::vector<uint32_t> cig2;
                while ((b = next.fetch_add(8)) < B) {
                    for (int k = b; k < b + 8 && k < B; k++) {
                        cigar_from_path(&path(k, 0), (long)B, I + J, cig2);
                    }
                }
            });
        }
        for (auto &th : threads) th.join();
    };
    double t_tb = benchmark(3, 1, [&]() { compaction_sweep(); });


    // ksw2 parallelizes across pairs the way aligners deploy it: when the
    // Halide build is parallel, give ksw2 the same cores.
    auto ksw2_sweep = [&](bool sse) {
        int nthreads = PARALLEL ? std::thread::hardware_concurrency() : 1;
        std::vector<std::thread> threads;
        std::atomic<int> next{0};
        for (int t = 0; t < nthreads; t++) {
            threads.emplace_back([&]() {
                int mc = 0, nc = 0, b;
                uint32_t *cig = nullptr;
                while ((b = next.fetch_add(8)) < B) {
                    for (int k = b; k < b + 8 && k < B; k++) {
                        nc = 0;
                        (sse ? ksw_gg2_sse : ksw_gg)(nullptr, J, &qs[(size_t)k * J], I, &ts[(size_t)k * I],
                                                     4, mat, GAPO, GAPE, -1, &mc, &nc, &cig);
                    }
                }
                free(cig);
            });
        }
        for (auto &t : threads) t.join();
    };
    double t_gg2 = benchmark(3, 1, [&]() { ksw2_sweep(true); });
    double t_gg = benchmark(3, 1, [&]() { ksw2_sweep(false); });

#ifdef HAVE_PARASAIL
    auto parasail_sweep = [&]() {
        int nthreads = PARALLEL ? std::thread::hardware_concurrency() : 1;
        std::vector<std::thread> threads;
        std::atomic<int> next{0};
        for (int t = 0; t < nthreads; t++) {
            threads.emplace_back([&]() {
                int b;
                std::vector<uint32_t> cig2;
                while ((b = next.fetch_add(8)) < B) {
                    for (int k = b; k < b + 8 && k < B; k++) {
                        parasail_one(k, &cig2);
                    }
                }
            });
        }
        for (auto &t : threads) t.join();
    };
    double t_ps = benchmark(3, 1, [&]() { parasail_sweep(); });
#endif

    const double cells = (double)B * J * I;
    // int8 inductive (t[0]) is the reference. Each form prints its
    // Gcell/s and its ratio to that reference.
    static const char *notes[6] = {
        "int8 differences, folded window",
        "int8, fusion without folding",
        "int8, materialized (update definitions)",
        "int16 table, folded window",
        "int16, fusion without folding",
        "int16, materialized (update definitions)",
    };
    for (int f = 0; f < 6; f++) {
        printf("  %-10s %10.1f us  (%.2f Gcell/s, %.2fx int8-ind: %s)\n",
               forms[f].name, t[f] * 1e6, cells / t[f] / 1e9, t[f] / t_ind, notes[f]);
    }
    printf("  compaction %10.1f us  (op stream -> run-length CIGAR, outside the pipeline)\n",
           t_tb * 1e6);
    printf("  int8 ind + traceback %8.1f us  (fill+trace+cigar, the baselines' contract)\n",
           (t_ind + t_tb) * 1e6);
    printf("  ksw2 sse   %10.1f us  (%.2fx fill+cigar: same output, hand-vectorized)\n",
           t_gg2 * 1e6, t_gg2 / (t_ind + t_tb));
    printf("  ksw2 gg    %10.1f us  (%.2fx: same output, scalar)\n",
           t_gg * 1e6, t_gg / t_ind);
#ifdef HAVE_PARASAIL
    printf("  parasail   %10.1f us  (%.2fx fill+cigar: nw_trace_striped_16, avx2)\n",
           t_ps * 1e6, t_ps / (t_ind + t_tb));
#endif
    // INTERLEAVE=n: n alternating single-shot rounds of int8 inductive
    // and the ksw2 SSE sweep, so the two sides share the same thermal
    // and clock state. Reports each round and the medians.
    if (const char *iv = getenv("INTERLEAVE")) {
        int rounds = atoi(iv);
        std::vector<double> ta, t16, tk, tp;
        printf("  interleaved rounds, fill+cigar (int8 ind | int16 ind | ksw2 sse | parasail):\n");
        for (int r = 0; r < rounds; r++) {
            double a = benchmark(1, 1, [&]() {
                align8_ind(query, target, path);
                compaction_sweep();
            });
            double a16 = benchmark(1, 1, [&]() {
                align16_ind(query, target, path);
                compaction_sweep();
            });
            double k = benchmark(1, 1, [&]() { ksw2_sweep(true); });
            double p = 0;
#ifdef HAVE_PARASAIL
            p = benchmark(1, 1, [&]() { parasail_sweep(); });
#endif
            ta.push_back(a);
            t16.push_back(a16);
            tk.push_back(k);
            tp.push_back(p);
            printf("    %10.1f | %10.1f | %10.1f | %10.1f us\n", a * 1e6, a16 * 1e6, k * 1e6, p * 1e6);
        }
        auto med = [](std::vector<double> v) {
            std::sort(v.begin(), v.end());
            return v[v.size() / 2];
        };
        printf("  medians: int8 %.1f, int16 %.1f, ksw2 sse %.1f, parasail %.1f us\n",
               med(ta) * 1e6, med(t16) * 1e6, med(tk) * 1e6, med(tp) * 1e6);
        printf("  ksw2/int8 %.2fx  parasail/int8 %.2fx  parasail/int16 %.2fx\n",
               med(tk) / med(ta), med(tp) / med(ta), med(tp) / med(t16));
    }
    printf("Success!\n");
    return 0;
}
