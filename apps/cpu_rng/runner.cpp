// Checks the three forms bit-exactly against a scalar xoshiro256++
// reference and benchmarks them, plus the same scalar loop as a timing
// baseline. The output buffer is sized well past the last level cache.

#include "HalideBuffer.h"
#include "HalideRuntime.h"
#include "../support/bench_harness.h"

#include "rng_ind.h"
#include "rng_rdom.h"
#include "rng_unf.h"

#include <immintrin.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

#ifndef PARALLEL
#define PARALLEL 0
#endif

using Halide::Runtime::Buffer;

#ifndef LANES
#define LANES 32
#endif
#ifndef STEPS
#define STEPS (4 << 20)
#endif

namespace {

constexpr int L = LANES;
constexpr int T = STEPS;

}  // namespace

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
                uint32_t w = (uint32_t)(r >> (32 * h));
                ref(2 * l + h, t) = (float)(w >> 8) * 0x1.0p-24f;
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

// Julia's bulk generator, ported to AVX-512 intrinsics: xoshiro_bulk_simd
// in the Random stdlib (stdlib/Random/src/XoshiroSimd.jl, Julia 1.12,
// https://github.com/JuliaLang/julia/blob/v1.12.7/stdlib/Random/src/XoshiroSimd.jl),
// which runs eight substreams of Vigna's xoshiro256++
// (https://prng.di.unimi.it/xoshiro256plusplus.c) interleaved across one
// vector and projects each 32-bit half to a float as _bits2float does:
// the top 24 bits, converted and scaled by 2^-24. The port is byte-exact
// with rand! (make LANES=8 test_julia). A task owns a group of stream
// blocks and walks every step for them: the blocks stay live and advance
// together, so every step stores full cache lines of output in order.
// Under PAR the groups go across the Halide runtime's thread pool, the
// same persistent pool the Halide forms run on.
constexpr int NB = L / 8;
static_assert(L % 8 == 0, "eight-lane blocks");
// Four blocks, 32 streams, per task: the serial configuration's width,
// whose state is sixteen vector registers.
constexpr int BLOCKS_PER_TASK = NB < 4 ? NB : 4;
constexpr int NTASKS = NB / BLOCKS_PER_TASK;
static_assert(NB % BLOCKS_PER_TASK == 0, "whole tasks");

void simd_fill_task(const Buffer<uint64_t> &seeds, Buffer<float> &out, int task) {
    // Raw addressing: the Buffer accessor marks the shared buffer host-dirty
    // on every store, which is a cache line all the tasks would fight over.
    float *const base = out.data();
    const ptrdiff_t row = out.dim(1).stride();
    const __m512 scale = _mm512_set1_ps(0x1.0p-24f);
    __m512i s[BLOCKS_PER_TASK][4];
    alignas(64) uint64_t tmp[8];
    const int blk0 = task * BLOCKS_PER_TASK;
    for (int b = 0; b < BLOCKS_PER_TASK; b++) {
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 8; j++) {
                tmp[j] = seeds(i, (blk0 + b) * 8 + j);
            }
            s[b][i] = _mm512_load_si512(tmp);
        }
    }
    for (int t = 0; t < T; t++) {
        for (int b = 0; b < BLOCKS_PER_TASK; b++) {
            const int blk = blk0 + b;
            __m512i (&sb)[4] = s[b];
            __m512i r = _mm512_add_epi64(
                _mm512_rol_epi64(_mm512_add_epi64(sb[0], sb[3]), 23),
                sb[0]);
            // Both halves of each 64-bit result are the same shift of a
            // 32-bit word, and little-endian order interleaves them as
            // consecutive output lanes: one full-width store per step.
            __m512i bits = _mm512_srli_epi32(r, 8);
            __m512 f = _mm512_mul_ps(_mm512_cvtepu32_ps(bits), scale);
            _mm512_store_ps(base + blk * 16 + t * row, f);
            __m512i t17 = _mm512_slli_epi64(sb[1], 17);
            sb[2] = _mm512_xor_si512(sb[2], sb[0]);
            sb[3] = _mm512_xor_si512(sb[3], sb[1]);
            sb[1] = _mm512_xor_si512(sb[1], sb[2]);
            sb[0] = _mm512_xor_si512(sb[0], sb[3]);
            sb[2] = _mm512_xor_si512(sb[2], t17);
            sb[3] = _mm512_rol_epi64(sb[3], 45);
        }
    }
}

void simd_fill(const Buffer<uint64_t> &seeds, Buffer<float> &out) {
#if PARALLEL
    auto task = [&](int k) { simd_fill_task(seeds, out, k); };
    halide_do_par_for(
        nullptr, [](void *, int k, uint8_t *closure) {
            (*(decltype(task) *)closure)(k);
            return 0;
        },
        0, NTASKS, (uint8_t *)&task);
#else
    for (int k = 0; k < NTASKS; k++) {
        simd_fill_task(seeds, out, k);
    }
#endif
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
    // With JULIA_SEEDS set, take the eight stream states Julia's rand!
    // forks from its master RNG (dumped by julia_ref.jl) instead of
    // seeding with splitmix64, so the output must match Julia's byte for
    // byte. Julia always forks eight SIMD substreams, so LANES must be 8.
    const char *julia_seeds = getenv("JULIA_SEEDS");
    const char *julia_ref = getenv("JULIA_REF");
    if (julia_seeds) {
        if (L != 8) {
            printf("JULIA_SEEDS requires LANES=8 (Julia forks eight substreams)\n");
            return 1;
        }
        FILE *f = fopen(julia_seeds, "rb");
        if (!f || fread(&seeds(0, 0), 8, 4 * L, f) != 4 * L) {
            printf("failed to read %s\n", julia_seeds);
            return 1;
        }
        fclose(f);
    } else {
        uint64_t sm = 0x853c49e6748fea9bull;
        for (int l = 0; l < L; l++) {
            for (int i = 0; i < 4; i++) {
                seeds(i, l) = splitmix64(sm);
            }
        }
    }
    Buffer<float> y(2 * L, T), ref(2 * L, T);
    printf("%d streams x %d steps, two floats per step (%.0f MB of floats, "
           "%.0f MB of state per trajectory)\n",
           L, T, 2 * L * (double)T * 4 / 1e6, L * (double)T * 32 / 1e6);

    reference(seeds, ref);
    double t_ref = hb::bench_s([&]() { reference(seeds, ref); });

    if (julia_ref) {
        FILE *f = fopen(julia_ref, "rb");
        std::vector<float> jref(2 * L * (size_t)T);
        if (!f || fread(jref.data(), 4, jref.size(), f) != jref.size()) {
            printf("failed to read %s\n", julia_ref);
            return 1;
        }
        fclose(f);
        for (int t = 0; t < T; t++) {
            for (int l = 0; l < 2 * L; l++) {
                if (ref(l, t) != jref[t * 2 * (size_t)L + l]) {
                    printf("  julia      MISMATCH at lane %d step %d: %a vs %a\n",
                           l, t, ref(l, t), jref[t * 2 * (size_t)L + l]);
                    return 1;
                }
            }
        }
        printf("  julia      bit-exact ok (vs Random.rand! on Xoshiro(1234))\n");
    }

    simd_fill(seeds, y);
    if (!check(y, ref, "julia port")) return 1;
    double t_simd = hb::bench_s([&]() { simd_fill(seeds, y); });

    rng_ind(seeds, y);
    if (!check(y, ref, "inductive")) return 1;
    double t_ind = hb::bench_s([&]() { rng_ind(seeds, y); });

    rng_unf(seeds, y);
    if (!check(y, ref, "unfolded")) return 1;
    double t_unf = hb::bench_s([&]() { rng_unf(seeds, y); });

    rng_rdom(seeds, y);
    if (!check(y, ref, "rdom")) return 1;
    double t_rdom = hb::bench_s([&]() { rng_rdom(seeds, y); });

    const double gb = 2 * L * (double)T * 4 / 1e9;
    printf("  inductive  %10.1f us  (%.1f GB/s of output)\n",
           t_ind * 1e6, gb / t_ind);
    printf("  unfolded   %10.1f us  (%.2fx: fusion without folding)\n",
           t_unf * 1e6, t_unf / t_ind);
    printf("  rdom       %10.1f us  (%.2fx the inductive time)\n",
           t_rdom * 1e6, t_rdom / t_ind);
    printf("  julia port %10.1f us  (%.2fx: Julia's XoshiroSimd kernel in AVX-512 intrinsics%s)\n",
           t_simd * 1e6, t_simd / t_ind, PARALLEL ? ", threaded" : "");
    printf("  scalar C++ %10.1f us  (%.2fx: same RNG, the reference loop)\n",
           t_ref * 1e6, t_ref / t_ind);
    printf("Success!\n");
    return 0;
}
