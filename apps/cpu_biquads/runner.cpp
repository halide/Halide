// Benchmarks and checks the two forms of the biquad cascade against a
// double precision reference. The channel count times sample count is
// chosen so the signal does not fit in the last level cache.

#include "HalideBuffer.h"
#include "HalideRuntime.h"
#include "halide_benchmark.h"

#include "biquads_ind.h"
#include "biquads_rdom.h"
#include "biquads_unf.h"

#ifdef HAVE_FFF
#include "fff_biquads.h"
#endif
#ifdef HAVE_IPP
#include "ipps.h"
#endif

#include <atomic>
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
#ifndef PARALLEL
#define PARALLEL 0
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
             const Buffer<float> &y, const char *what, double tol = 2e-4) {
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
           err < tol ? "ok" : "FAILED");
    if (err >= tol) {
        exit(1);
    }
    return err;
}

}  // namespace


// The signals: one 64-byte-aligned block each, shared by every variant
// whose layout is channels innermost (the Halide forms, and the library
// cascade when its block is the whole channel set).
float *signal_alloc() {
    return (float *)aligned_alloc(64, (size_t)C * S * sizeof(float));
}

int main(int argc, char **argv) {
    float *xmem = signal_alloc(), *ymem = signal_alloc();
    Buffer<float> x(xmem, C, S), y(ymem, C, S), sos(6, N);
    make_sos(sos);
    fill_input(x);

    printf("%d channels x %d samples (%.0f MB), %d sections\n", C, S,
           C * (double)S * 4 / 1e6, N);

    biquads_ind(x, sos, y);
    check(x, sos, y, "inductive");
    double t_ind = benchmark(10, 1, [&]() { biquads_ind(x, sos, y); });

    biquads_unf(x, sos, y);
    check(x, sos, y, "unfolded");
    double t_unf = benchmark(10, 1, [&]() { biquads_unf(x, sos, y); });

    biquads_rdom(x, sos, y);
    check(x, sos, y, "rdom");
    double t_rdom = benchmark(10, 1, [&]() { biquads_rdom(x, sos, y); });

#ifdef HAVE_IPP
    // Intel IPP's multi-channel IIR: one biquad-cascade state per channel
    // (the same sos taps), all channels in one ippsIIR_32f_P call over
    // planar signals. Under PAR the channels are dealt across threads,
    // each thread making its own call over its share.
    // Planar copies of the signal, one aligned block each.
    float *xin = signal_alloc(), *yout = signal_alloc();
    for (int c = 0; c < C; c++) {
        for (int n = 0; n < S; n++) {
            xin[(size_t)c * S + n] = x(c, n);
        }
    }
    std::vector<float> taps(6 * N);
    for (int k = 0; k < N; k++) {
        for (int j = 0; j < 6; j++) {
            taps[6 * k + j] = sos(j, k);
        }
    }
    int state_bytes = 0;
    ippsIIRGetStateSize_BiQuad_32f(N, &state_bytes);
    std::vector<std::vector<Ipp8u>> state_buf(C, std::vector<Ipp8u>(state_bytes));
    std::vector<IppsIIRState_32f *> states(C);
    std::vector<const float *> src(C);
    std::vector<float *> dst(C);
    for (int c = 0; c < C; c++) {
        src[c] = xin + (size_t)c * S;
        dst[c] = yout + (size_t)c * S;
    }
    // Under PAR the channels are dealt in chunks across the Halide runtime's
    // thread pool, the same persistent pool the Halide forms run on; each
    // chunk is one multi-channel call. Fresh delay lines each run: a
    // cascade is stateful.
    const int ipp_chunks = PARALLEL ? std::min<int>(C, std::thread::hardware_concurrency()) : 1;
    auto ipp_chunk = [&](int t) {
        int c0 = (int)((long)C * t / ipp_chunks), c1 = (int)((long)C * (t + 1) / ipp_chunks);
        for (int c = c0; c < c1; c++) {
            ippsIIRInit_BiQuad_32f(&states[c], taps.data(), N, nullptr, state_buf[c].data());
        }
        ippsIIR_32f_P(src.data() + c0, dst.data() + c0, S, c1 - c0, states.data() + c0);
    };
    auto ipp_run = [&]() {
        if (PARALLEL) {
            halide_do_par_for(
                nullptr, [](void *, int t, uint8_t *closure) {
                    (*(decltype(ipp_chunk) *)closure)(t);
                    return 0;
                },
                0, ipp_chunks, (uint8_t *)&ipp_chunk);
        } else {
            ipp_chunk(0);
        }
    };
    ipp_run();
    Buffer<float> yipp(C, S);
    for (int c = 0; c < C; c++) {
        for (int n = 0; n < S; n++) {
            yipp(c, n) = yout[(size_t)c * S + n];
        }
    }
    // IPP runs the cascade in single precision in its own filter
    // structure, so its rounding differs from the direct-form-one forms
    // above by a few ulps of the signal: a looser tolerance, same double
    // reference.
    check(x, sos, yipp, "ipp", 1e-3);
    double t_ipp = benchmark(10, 1, ipp_run);
    free(xin);
    free(yout);
#endif

#ifdef HAVE_FFF
    // The "Finding Fast Filters" library's strided IIR cascade: a block of
    // FFF_STRIDE channels is one 1-D signal with that stride, sections as
    // sparse FIRs (numerators) cascaded with the library's serial
    // second-order IIRs. The signal is handed over block-major, channels
    // innermost within a block, and the blocks are dealt to threads.
    const int fstride = FffBiquads::stride();
    const int nblocks = C / fstride;
    // With one block of all the channels the layout is the signal's own;
    // narrower blocks need a block-major copy.
    const bool blocked = nblocks > 1;
    float *xb = blocked ? signal_alloc() : xmem;
    float *yb = blocked ? signal_alloc() : ymem;
    if (blocked) {
        for (int b = 0; b < nblocks; b++) {
            for (int n = 0; n < S; n++) {
                for (int c = 0; c < fstride; c++) {
                    xb[((size_t)b * S + n) * fstride + c] = x(b * fstride + c, n);
                }
            }
        }
    }
    std::vector<float> sos_flat(6 * N);
    for (int k = 0; k < N; k++) {
        for (int j = 0; j < 6; j++) {
            sos_flat[6 * k + j] = sos(j, k);
        }
    }
    FffBiquads fff(nblocks, sos_flat);
    // Blocks across the Halide runtime's thread pool under PAR: the same
    // persistent pool the Halide forms run on.
    auto fff_block = [&](int b) { fff.run_block(b, xb, yb, S); };
    auto fff_run = [&]() {
        if (PARALLEL) {
            halide_do_par_for(
                nullptr, [](void *, int b, uint8_t *closure) {
                    (*(decltype(fff_block) *)closure)(b);
                    return 0;
                },
                0, nblocks, (uint8_t *)&fff_block);
        } else {
            for (int b = 0; b < nblocks; b++) {
                fff_block(b);
            }
        }
    };
    fff_run();
    Buffer<float> yfff(C, S);
    for (int b = 0; b < nblocks; b++) {
        for (int n = 0; n < S; n++) {
            for (int c = 0; c < fstride; c++) {
                yfff(b * fstride + c, n) = yb[((size_t)b * S + n) * fstride + c];
            }
        }
    }
    check(x, sos, yfff, "fff");
    double t_fff = benchmark(10, 1, fff_run);
    if (blocked) {
        free(xb);
        free(yb);
    }
#endif

    const double gb = C * (double)S * 4 / 1e9;
    printf("  inductive  %10.1f us  (%.1f GB/s of signal each way)\n",
           t_ind * 1e6, 2 * gb / t_ind);
    printf("  unfolded   %10.1f us  (%.2fx: fusion without folding)\n",
           t_unf * 1e6, t_unf / t_ind);
    printf("  rdom       %10.1f us  (%.2fx the inductive time)\n",
           t_rdom * 1e6, t_rdom / t_ind);
#ifdef HAVE_IPP
    printf("  ipp        %10.1f us  (%.2fx: ippsIIR_32f_P, biquad cascade per channel%s)\n",
           t_ipp * 1e6, t_ipp / t_ind, PARALLEL ? ", threaded" : "");
#endif
#ifdef HAVE_FFF
    printf("  fff        %10.1f us  (%.2fx: Finding Fast Filters strided cascade, %d-channel blocks%s)\n",
           t_fff * 1e6, t_fff / t_ind, fstride, PARALLEL ? ", threaded" : "");
#endif
    printf("Success!\n");
    return 0;
}
