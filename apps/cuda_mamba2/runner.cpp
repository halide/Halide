// Linked against libHalide only for float16_t, as apps/cuda_attention does.
#include "Halide.h"
#include "HalideBuffer.h"
#include "halide_benchmark.h"
#include "mamba2.h"

#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

using namespace Halide::Runtime;
using Halide::float16_t;

#ifndef SEQ
#define SEQ 4096
#endif
#ifndef STATE
#define STATE 64
#endif
#ifndef CHANNELS
#define CHANNELS 64
#endif
#ifndef CHUNK
#define CHUNK 64
#endif
#ifndef HEADS
#define HEADS 128
#endif
#ifndef GROUPS
#define GROUPS 1
#endif

constexpr int seq = SEQ, state = STATE, channels = CHANNELS, chunk = CHUNK, heads = HEADS;
constexpr int groups = GROUPS;
constexpr int num_chunks = seq / chunk;

// The recurrence as written, one step at a time, for one head.
static void reference_head(const Buffer<float16_t> &X, const Buffer<float16_t> &Bm,
                           const Buffer<float16_t> &Cm, const Buffer<float> &Delta,
                           float A, int b, std::vector<float> &out) {
    const int g = b / (heads / groups);
    std::vector<float> h(state * channels, 0.f);
    for (int n = 0; n < seq; n++) {
        float dt = Delta(n, b);
        float a = std::exp(dt * A);
        for (int pp = 0; pp < state; pp++) {
            float db = dt * (float)Bm(pp, n, g);
            for (int dd = 0; dd < channels; dd++) {
                h[pp * channels + dd] = a * h[pp * channels + dd] + db * (float)X(dd, n, b);
            }
        }
        for (int dd = 0; dd < channels; dd++) {
            float y = 0;
            for (int pp = 0; pp < state; pp++) {
                y += (float)Cm(pp, n, g) * h[pp * channels + dd];
            }
            out[dd + n * channels] = y;
        }
    }
}

int main(int argc, char **argv) {
    Buffer<float16_t> X(channels, seq, heads), Bm(state, seq, groups), Cm(state, seq, groups);
    Buffer<float> Delta(seq, heads), A(heads);
    Buffer<float16_t> result(channels, chunk, num_chunks, heads);

    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(-1.f, 1.f);
    std::uniform_real_distribution<float> delta_dist(0.001f, 0.1f);

    for (int b = 0; b < heads; b++) {
        A(b) = -1.f - 0.5f * (b % 3);
        for (int n = 0; n < seq; n++) {
            Delta(n, b) = delta_dist(rng);
            for (int dd = 0; dd < channels; dd++) {
                X(dd, n, b) = float16_t(dist(rng));
            }
            if (b < groups) {
                for (int pp = 0; pp < state; pp++) {
                    Bm(pp, n, b) = float16_t(dist(rng));
                    Cm(pp, n, b) = float16_t(dist(rng));
                }
            }
        }
    }

    if (mamba2(X, Bm, Cm, Delta, A, result) != 0) {
        printf("mamba2 returned an error\n");
        return 1;
    }
    result.copy_to_host();

    // Checking every head against a step-at-a-time reference would cost more
    // than the pipeline does, so check a few.
    const int checked = 2;
    double max_err = 0, max_mag = 0;
    int reported = 0;
    std::vector<float> ref(seq * channels);
    for (int b = 0; b < checked; b++) {
        reference_head(X, Bm, Cm, Delta, A(b), b, ref);
        for (int n = 0; n < seq; n++) {
            for (int dd = 0; dd < channels; dd++) {
                double want = ref[dd + n * channels];
                double got = (float)result(dd, n % chunk, n / chunk, b);
                if (std::abs(got - want) > 1e-2 && reported < 6) {
                    printf("  mismatch head %d chunk %d pos %d chan %d: %f vs %f\n",
                           b, n / chunk, n % chunk, dd, got, want);
                    reported++;
                }
                max_err = std::max(max_err, std::abs(got - want));
                max_mag = std::max(max_mag, std::abs(want));
            }
        }
    }
    printf("max error %.3e against a magnitude of %.3e over %d head(s)\n",
           max_err, max_mag, checked);
    bool ok = max_err < 1e-3 * std::max(1.0, max_mag);

    // Two multiplies of chunk x state x chunk and chunk x chunk x channels for
    // the scores, and two of state x chunk x channels and chunk x state x
    // channels for the state, per chunk. All but the scores are per head; the
    // scores are per group, because every head of a group has the same ones.
    const double flops =
        2.0 * num_chunks * chunk *
        ((double)groups * chunk * state +
         heads * ((double)chunk * channels + 2.0 * state * channels));
    double t = Halide::Tools::benchmark(10, 10, [&]() {
        mamba2(X, Bm, Cm, Delta, A, result);
        result.device_sync();
    });
    printf("  Halide mamba2 %30.0f GFlop/s %10.1f us\n", flops / t / 1e9, t * 1e6);

    printf("%s\n", ok ? "Success!" : "FAILED");
    return ok ? 0 : 1;
}
