// Verifies and benchmarks the batched affine-gap aligner against ksw2
// (the kernel family behind minimap2). The Halide direction planes are
// checked byte-for-byte against ksw_gg's backtrack matrix, and the
// CIGARs reconstructed from them must match ksw2's exactly.

#include "HalideBuffer.h"
#include "halide_benchmark.h"

#include "align_diff8.h"
#include "align_ind.h"
#include "align_rdom.h"
#include "align_unf.h"

extern "C" {
#include "ksw2/ksw2.h"
int ksw_gg(void *km, int qlen, const uint8_t *query, int tlen, const uint8_t *target, int8_t m, const int8_t *mat, int8_t gapo, int8_t gape, int w, int *m_cigar_, int *n_cigar_, uint32_t **cigar_);
int ksw_gg2_sse(void *km, int qlen, const uint8_t *query, int tlen, const uint8_t *target, int8_t m, const int8_t *mat, int8_t q, int8_t e, int w, int *m_cigar_, int *n_cigar_, uint32_t **cigar_);
}

#include <cstdio>
#include <malloc.h>
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

extern "C" void halide_free(void *, void *ptr) {
    char *base = (char *)ptr - 128;
    size_t size = ((size_t *)base)[0];
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

// ksw_backtrack for the unbanded row-major case, over one pair's
// direction plane accessed through a stride.
void backtrack(const uint8_t *p, long jstride, long istride, int i0, int j0,
               std::vector<uint32_t> &cigar) {
    cigar.clear();
    auto push = [&](uint32_t op, int len) {
        if (!cigar.empty() && (cigar.back() & 0xf) == op) {
            cigar.back() += len << 4;
        } else {
            cigar.push_back(len << 4 | op);
        }
    };
    int i = i0, j = j0, state = 0;
    while (i >= 0 && j >= 0) {
        uint32_t tmp = p[j * jstride + i * istride];
        if (state == 0) state = tmp & 7;
        else if (!(tmp >> (state + 2) & 1)) state = 0;
        if (state == 0) state = tmp & 7;
        if (state == 0) push(0 /*M*/, 1), --i, --j;
        else if (state == 1) push(2 /*D*/, 1), --i;
        else push(1 /*I*/, 1), --j;
    }
    if (i >= 0) push(2, i + 1);
    if (j >= 0) push(1, j + 1);
    std::reverse(cigar.begin(), cigar.end());
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

    Buffer<uint8_t> dir(B, J, I);
    std::vector<uint32_t> cig;

    auto check = [&](const char *what) {
        for (int b = 0; b < B; b++) {
            backtrack(&dir(b, 0, 0), (long)B, (long)B * J, I - 1, J - 1, cig);
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

    align_ind(query, target, dir);
    if (!check("inductive")) return 1;
    double t_ind = benchmark(3, 1, [&]() { align_ind(query, target, dir); });

    align_unf(query, target, dir);
    if (!check("unfolded")) return 1;
    double t_unf = benchmark(3, 1, [&]() { align_unf(query, target, dir); });

    align_rdom(query, target, dir);
    if (!check("rdom")) return 1;
    double t_rdom = benchmark(3, 1, [&]() { align_rdom(query, target, dir); });

    align_diff8(query, target, dir);
    if (!check("diff8")) return 1;
    double t_d8 = benchmark(3, 1, [&]() { align_diff8(query, target, dir); });

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

    const double cells = (double)B * J * I;
    printf("  inductive  %10.1f us  (%.2f Gcell/s)\n", t_ind * 1e6, cells / t_ind / 1e9);
    printf("  diff8      %10.1f us  (%.2f Gcell/s, %.2fx: int8 differences)\n",
           t_d8 * 1e6, cells / t_d8 / 1e9, t_d8 / t_ind);
    printf("  unfolded   %10.1f us  (%.2fx: fusion without folding)\n",
           t_unf * 1e6, t_unf / t_ind);
    printf("  rdom       %10.1f us  (%.2fx the inductive time)\n",
           t_rdom * 1e6, t_rdom / t_ind);
    printf("  ksw2 sse   %10.1f us  (%.2fx: same output, hand-vectorized)\n",
           t_gg2 * 1e6, t_gg2 / t_ind);
    printf("  ksw2 gg    %10.1f us  (%.2fx: same output, scalar)\n",
           t_gg * 1e6, t_gg / t_ind);
    printf("Success!\n");
    return 0;
}
