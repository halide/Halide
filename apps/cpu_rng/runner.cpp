// Checks the three forms bit-exactly against a scalar xoshiro256++
// reference and benchmarks them, plus the same scalar loop as a timing
// baseline. The output buffer is sized well past the last level cache.

#include "HalideBuffer.h"
#include "halide_benchmark.h"

#include "rng_ind.h"
#include "rng_rdom.h"
#include "rng_unf.h"

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
            ref(l, t) = (float)(r >> 40) * (1.f / (1 << 24));
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

bool check(const Buffer<float> &y, const Buffer<float> &ref, const char *what) {
    for (int l = 0; l < L; l++) {
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
    Buffer<float> y(L, T), ref(L, T);
    printf("%d streams x %d steps (%.0f MB of floats, %.0f MB of state "
           "per trajectory)\n",
           L, T, L * (double)T * 4 / 1e6, L * (double)T * 32 / 1e6);

    reference(seeds, ref);
    double t_ref = benchmark(3, 1, [&]() { reference(seeds, ref); });

    rng_ind(seeds, y);
    if (!check(y, ref, "inductive")) return 1;
    double t_ind = benchmark(3, 1, [&]() { rng_ind(seeds, y); });

    rng_unf(seeds, y);
    if (!check(y, ref, "unfolded")) return 1;
    double t_unf = benchmark(3, 1, [&]() { rng_unf(seeds, y); });

    rng_rdom(seeds, y);
    if (!check(y, ref, "rdom")) return 1;
    double t_rdom = benchmark(3, 1, [&]() { rng_rdom(seeds, y); });

    const double gb = L * (double)T * 4 / 1e9;
    printf("  inductive  %10.1f us  (%.1f GB/s of output)\n",
           t_ind * 1e6, gb / t_ind);
    printf("  unfolded   %10.1f us  (%.2fx: fusion without folding)\n",
           t_unf * 1e6, t_unf / t_ind);
    printf("  rdom       %10.1f us  (%.2fx the inductive time)\n",
           t_rdom * 1e6, t_rdom / t_ind);
    printf("  scalar C++ %10.1f us  (%.2fx the inductive time)\n",
           t_ref * 1e6, t_ref / t_ind);
    printf("Success!\n");
    return 0;
}
