// Benchmarks and checks the two forms of the biquad cascade against a
// double precision reference. The channel count times sample count is
// chosen so the signal does not fit in the last level cache.

#include "HalideBuffer.h"
#include "halide_benchmark.h"

#include "biquads_ind.h"
#include "biquads_rdom.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

using Halide::Runtime::Buffer;
using Halide::Tools::benchmark;

#ifndef CHANNELS
#define CHANNELS 32
#endif
#ifndef SAMPLES
#define SAMPLES (8 << 20)
#endif
#ifndef SECTIONS
#define SECTIONS 8
#endif

namespace {

constexpr int C = CHANNELS;
constexpr int S = SAMPLES;
constexpr int N = SECTIONS;

// A cascade of peaking equalizers from the usual audio cookbook formulas,
// center frequencies spread across the band, alternating boost and cut.
// Every section is comfortably stable.
void make_sos(Buffer<float> &sos) {
    const double fs = 48000.0, q = 0.8;
    for (int k = 0; k < N; k++) {
        double f0 = 120.0 * std::pow(8000.0 / 120.0,
                                     N > 1 ? (double)k / (N - 1) : 0.0);
        double gain_db = (k % 2) ? 3.0 : -3.0;
        double A = std::pow(10.0, gain_db / 40);
        double w0 = 2.0 * M_PI * f0 / fs;
        double alpha = std::sin(w0) / (2 * q);
        double b0 = 1 + alpha * A, b1 = -2 * std::cos(w0), b2 = 1 - alpha * A;
        double a0 = 1 + alpha / A, a1 = b1, a2 = 1 - alpha / A;
        sos(0, k) = (float)(b0 / a0);
        sos(1, k) = (float)(b1 / a0);
        sos(2, k) = (float)(b2 / a0);
        sos(3, k) = 1.f;
        sos(4, k) = (float)(a1 / a0);
        sos(5, k) = (float)(a2 / a0);
    }
}

void fill_input(Buffer<float> &x) {
    const int nthreads = std::min(32u, std::thread::hardware_concurrency());
    std::vector<std::thread> pool;
    for (int ti = 0; ti < nthreads; ti++) {
        pool.emplace_back([&, ti]() {
            for (int c = ti; c < C; c += nthreads) {
                uint32_t state = 0x9e3779b9u ^ (c * 2654435761u);
                for (int n = 0; n < S; n++) {
                    state = state * 1664525u + 1013904223u;
                    x(c, n) = (float)(int32_t)state * (1.f / 2147483648.f);
                }
            }
        });
    }
    for (auto &th : pool) {
        th.join();
    }
}

// The same cascade at double precision, one channel at a time.
double check(const Buffer<float> &x, const Buffer<float> &sos,
             const Buffer<float> &y, const char *what) {
    const int check_channels = std::min(C, 4);
    std::vector<double> errs(check_channels, 0.0);
    std::vector<std::thread> pool;
    for (int c = 0; c < check_channels; c++) {
        pool.emplace_back([&, c]() {
            std::vector<double> u(S), v(S);
            for (int n = 0; n < S; n++) {
                u[n] = x(c, n);
            }
            for (int k = 0; k < N; k++) {
                double b0 = sos(0, k), b1 = sos(1, k), b2 = sos(2, k);
                double a1 = sos(4, k), a2 = sos(5, k);
                double y1 = 0, y2 = 0, x1 = 0, x2 = 0;
                for (int n = 0; n < S; n++) {
                    double out = b0 * u[n] + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
                    x2 = x1;
                    x1 = u[n];
                    y2 = y1;
                    y1 = out;
                    v[n] = out;
                }
                std::swap(u, v);
            }
            double err = 0;
            for (int n = 0; n < S; n++) {
                err = std::max(err, std::abs(u[n] - (double)y(c, n)));
            }
            errs[c] = err;
        });
    }
    for (auto &th : pool) {
        th.join();
    }
    double err = 0;
    for (double e : errs) {
        err = std::max(err, e);
    }
    printf("  %-10s max error %.3e %s\n", what, err,
           err < 2e-4 ? "ok" : "FAILED");
    if (err >= 2e-4) {
        exit(1);
    }
    return err;
}

}  // namespace

// The N-pass form allocates gigabytes of intermediates per realization;
// without a reusing allocator the benchmark would partly measure page
// faults. Freed blocks are kept for reuse and reclaimed at exit.
namespace {
std::vector<std::pair<size_t, void *>> &the_pool() {
    static std::vector<std::pair<size_t, void *>> pool;
    return pool;
}
}  // namespace

extern "C" void *halide_malloc(void *, size_t sz) {
    auto &pool = the_pool();
    for (auto &e : pool) {
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
    the_pool().push_back({((size_t *)p)[-2], p});
}

int main(int argc, char **argv) {
    Buffer<float> x(C, S), y(C, S), sos(6, N);
    make_sos(sos);
    fill_input(x);

    printf("%d channels x %d samples (%.0f MB), %d sections\n", C, S,
           C * (double)S * 4 / 1e6, N);

    biquads_ind(x, sos, y);
    check(x, sos, y, "inductive");
    double t_ind = benchmark(3, 1, [&]() { biquads_ind(x, sos, y); });

    biquads_rdom(x, sos, y);
    check(x, sos, y, "rdom");
    double t_rdom = benchmark(3, 1, [&]() { biquads_rdom(x, sos, y); });

    const double gb = C * (double)S * 4 / 1e9;
    printf("  inductive  %10.1f us  (%.1f GB/s of signal each way)\n",
           t_ind * 1e6, 2 * gb / t_ind);
    printf("  rdom       %10.1f us  (%.2fx the inductive time)\n",
           t_rdom * 1e6, t_rdom / t_ind);
    printf("Success!\n");
    return 0;
}
