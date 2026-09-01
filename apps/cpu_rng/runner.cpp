// Checks the three forms bit-exactly against a scalar xoshiro256++
// reference and benchmarks them, plus the same scalar loop as a timing
// baseline. The output buffer is sized well past the last level cache.

#include "HalideBuffer.h"
#include "halide_benchmark.h"

#include "rng_ind.h"
#include "rng_rdom.h"
#include "rng_unf.h"

#include <immintrin.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

using Halide::Runtime::Buffer;
using Halide::Tools::benchmark;

#ifndef LANES
#define LANES 32
#endif
#ifndef STEPS
#define STEPS (4 << 20)
#endif

namespace {

constexpr int L = LANES;
constexpr int T = STEPS;

// The reusing allocator, so the materialized form's gigabytes of state per
// realization are not charged as page faults.
std::vector<std::pair<size_t, void *>> &the_pool() {
    static std::vector<std::pair<size_t, void *>> pool;
    return pool;
}

}  // namespace

namespace {
std::mutex &pool_mutex() {
    static std::mutex m;
    return m;
}
}  // namespace

extern "C" void *halide_malloc(void *, size_t sz) {
    std::lock_guard<std::mutex> lock(pool_mutex());
    for (auto &e : the_pool()) {
        if (e.first == sz && e.second) {
            void *p = e.second;
            e.second = nullptr;
            return p;
        }
    }
    char *base = (char *)malloc(sz + 256);
    if (!base) {
        return nullptr;
    }
    char *p = (char *)(((uintptr_t)base + 128 + 127) & ~(uintptr_t)127);
    ((void **)p)[-1] = base;
    ((size_t *)p)[-2] = sz;
    return p;
}

extern "C" void halide_free(void *, void *p) {
    std::lock_guard<std::mutex> lock(pool_mutex());
    the_pool().push_back({((size_t *)p)[-2], p});
}

namespace {

uint64_t splitmix64(uint64_t &x) {
    uint64_t z = (x += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

inline uint64_t rotl(uint64_t x, int k) {
    return (x << k) | (x >> (64 - k));
}

// The scalar reference: per stream, output the projection of the current
// state, then advance.
void reference(const Buffer<uint64_t> &seeds, Buffer<float> &ref) {
    for (int l = 0; l < L; l++) {
        uint64_t s0 = seeds(0, l), s1 = seeds(1, l);
        uint64_t s2 = seeds(2, l), s3 = seeds(3, l);
        for (int t = 0; t < T; t++) {
            uint64_t r = rotl(s0 + s3, 23) + s0;
            for (int h = 0; h < 2; h++) {
                uint32_t bits = (((uint32_t)(r >> (32 * h))) >> 9) | 0x3f800000u;
                float f;
                memcpy(&f, &bits, 4);
                ref(2 * l + h, t) = f - 1.f;
            }
            uint64_t t17 = s1 << 17;
            s2 ^= s0;
            s3 ^= s1;
            s1 ^= s2;
            s0 ^= s3;
            s2 ^= t17;
            s3 = rotl(s3, 45);
        }
    }
}

// The same generator, hand-vectorized: eight streams per vector, two
// vectors interleaved, the layout the Halide schedule uses. This is the
// baseline a performance-minded C++ programmer would write.
void simd_fill(const Buffer<uint64_t> &seeds, Buffer<float> &out) {
    // All the stream blocks stay live and advance together per step, so
    // every step stores full cache lines of output in order.
    constexpr int NB = L / 8;
    static_assert(L % 8 == 0, "eight-lane blocks");
    const __m512i one = _mm512_set1_epi32(0x3f800000);
    const __m512 onef = _mm512_set1_ps(1.f);
    __m512i s[NB][4];
    alignas(64) uint64_t tmp[8];
    for (int blk = 0; blk < NB; blk++) {
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 8; j++) {
                tmp[j] = seeds(i, blk * 8 + j);
            }
            s[blk][i] = _mm512_load_si512(tmp);
        }
    }
    for (int t = 0; t < T; t++) {
        for (int blk = 0; blk < NB; blk++) {
            __m512i r = _mm512_add_epi64(
                _mm512_rol_epi64(_mm512_add_epi64(s[blk][0], s[blk][3]), 23),
                s[blk][0]);
            // Both halves of each 64-bit result are the same shift of a
            // 32-bit word, and little-endian order interleaves them as
            // consecutive output lanes: one full-width store per step.
            __m512i bits = _mm512_or_si512(
                _mm512_srli_epi32(r, 9), one);
            __m512 f = _mm512_sub_ps(_mm512_castsi512_ps(bits), onef);
            _mm512_storeu_ps(&out(blk * 16, t), f);
            __m512i t17 = _mm512_slli_epi64(s[blk][1], 17);
            s[blk][2] = _mm512_xor_si512(s[blk][2], s[blk][0]);
            s[blk][3] = _mm512_xor_si512(s[blk][3], s[blk][1]);
            s[blk][1] = _mm512_xor_si512(s[blk][1], s[blk][2]);
            s[blk][0] = _mm512_xor_si512(s[blk][0], s[blk][3]);
            s[blk][2] = _mm512_xor_si512(s[blk][2], t17);
            s[blk][3] = _mm512_rol_epi64(s[blk][3], 45);
        }
    }
}

bool check(const Buffer<float> &y, const Buffer<float> &ref, const char *what) {
    for (int l = 0; l < 2 * L; l++) {
        for (int t = 0; t < T; t++) {
            if (y(l, t) != ref(l, t)) {
                printf("  %-10s MISMATCH at lane %d step %d: %a vs %a\n",
                       what, l, t, y(l, t), ref(l, t));
                return false;
            }
        }
    }
    printf("  %-10s bit-exact ok\n", what);
    return true;
}

}  // namespace

int main(int argc, char **argv) {
    Buffer<uint64_t> seeds(4, L);
    uint64_t sm = 0x853c49e6748fea9bull;
    for (int l = 0; l < L; l++) {
        for (int i = 0; i < 4; i++) {
            seeds(i, l) = splitmix64(sm);
        }
    }
    Buffer<float> y(2 * L, T), ref(2 * L, T);
    printf("%d streams x %d steps, two floats per step (%.0f MB of floats, "
           "%.0f MB of state per trajectory)\n",
           L, T, 2 * L * (double)T * 4 / 1e6, L * (double)T * 32 / 1e6);

    reference(seeds, ref);
    double t_ref = benchmark(3, 1, [&]() { reference(seeds, ref); });

    simd_fill(seeds, y);
    if (!check(y, ref, "simd C++")) return 1;
    double t_simd = benchmark(3, 1, [&]() { simd_fill(seeds, y); });

    rng_ind(seeds, y);
    if (!check(y, ref, "inductive")) return 1;
    double t_ind = benchmark(3, 1, [&]() { rng_ind(seeds, y); });

    rng_unf(seeds, y);
    if (!check(y, ref, "unfolded")) return 1;
    double t_unf = benchmark(3, 1, [&]() { rng_unf(seeds, y); });

    rng_rdom(seeds, y);
    if (!check(y, ref, "rdom")) return 1;
    double t_rdom = benchmark(3, 1, [&]() { rng_rdom(seeds, y); });

    const double gb = 2 * L * (double)T * 4 / 1e9;
    printf("  inductive  %10.1f us  (%.1f GB/s of output)\n",
           t_ind * 1e6, gb / t_ind);
    printf("  unfolded   %10.1f us  (%.2fx: fusion without folding)\n",
           t_unf * 1e6, t_unf / t_ind);
    printf("  rdom       %10.1f us  (%.2fx the inductive time)\n",
           t_rdom * 1e6, t_rdom / t_ind);
    printf("  simd C++   %10.1f us  (%.2fx: same RNG, hand-vectorized)\n",
           t_simd * 1e6, t_simd / t_ind);
    printf("  scalar C++ %10.1f us  (%.2fx: same RNG, the reference loop)\n",
           t_ref * 1e6, t_ref / t_ind);
    printf("Success!\n");
    return 0;
}
