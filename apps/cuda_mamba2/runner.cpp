// Linked against libHalide only for float16_t, as apps/cuda_attention does.
#include "Halide.h"
#include "HalideBuffer.h"
#include "../support/bench_harness.h"
#include "mamba2.h"

#include <algorithm>
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

// Which heads to check against the reference. Checking all of them costs more
// than the pipeline does, so take a spread: the first two, one from the middle,
// and the last, which between them land in different groups whenever there is
// more than one.
static std::vector<int> heads_to_check() {
    std::vector<int> hs = {0, 1, heads / 2, heads - 1};
    std::sort(hs.begin(), hs.end());
    hs.erase(std::unique(hs.begin(), hs.end()), hs.end());
    return hs;
}

// Compare against the step-at-a-time reference. Returns the largest absolute
// error and the largest magnitude it was measured against.
static bool check(const Buffer<float16_t> &X, const Buffer<float16_t> &Bm,
                  const Buffer<float16_t> &Cm, const Buffer<float> &Delta,
                  const Buffer<float> &A, const Buffer<float16_t> &result,
                  const char *what, double rel_tol) {
    double max_err = 0, max_mag = 0;
    int reported = 0;
    std::vector<float> ref(seq * channels);
    const std::vector<int> hs = heads_to_check();
    for (int b : hs) {
        reference_head(X, Bm, Cm, Delta, A(b), b, ref);
        for (int n = 0; n < seq; n++) {
            for (int dd = 0; dd < channels; dd++) {
                double want = ref[dd + n * channels];
                double got = (float)result(dd, n % chunk, n / chunk, b);
                double err = std::abs(got - want);
                if (err > rel_tol * std::max(1.0, std::abs(want)) && reported < 6) {
                    printf("  mismatch head %d chunk %d pos %d chan %d: %f vs %f\n",
                           b, n / chunk, n % chunk, dd, got, want);
                    reported++;
                }
                max_err = std::max(max_err, err);
                max_mag = std::max(max_mag, std::abs(want));
            }
        }
    }
    bool ok = max_err <= rel_tol * std::max(1.0, max_mag);
    printf("%-28s max error %.3e against a magnitude of %.3e over %d heads: %s\n",
           what, max_err, max_mag, (int)hs.size(), ok ? "ok" : "FAILED");
    if (rel_tol == 0.0 && max_mag >= 2048) {
        printf("  the answers have grown past what half precision holds exactly, "
               "so this check is no longer one\n");
        ok = false;
    }
    return ok;
}

int main(int argc, char **argv) {
    Buffer<float16_t> X(channels, seq, heads), Bm(state, seq, groups), Cm(state, seq, groups);
    Buffer<float> Delta(seq, heads), A(heads);
    Buffer<float16_t> result(channels, chunk, num_chunks, heads);

    auto run = [&]() {
        if (mamba2(X, Bm, Cm, Delta, A, result) != 0) {
            printf("mamba2 returned an error\n");
            exit(1);
        }
        result.copy_to_host();
    };

    bool ok = true;

    // A structural check first. With no decay and whole-number inputs sparse
    // enough that nothing sums past what half precision holds exactly, every
    // value in the pipeline is an integer and the answer is exact. Anything
    // that lands in the wrong place - a chunk boundary off by one, a mask the
    // wrong way round, a head reading the wrong group - shows up as a
    // mismatch rather than hiding under a rounding tolerance.
    {
        std::mt19937 rng(7);
        // Half precision holds whole numbers exactly only up to 2048, and an
        // output is a sum over the state of a sum over the sequence, so how
        // dense the inputs can be depends on both. Aim for a hundred or so and
        // the tail of the distribution stays well inside the range.
        const double density =
            std::cbrt(64.0 / (1.5 * (double)state * (double)seq));
        std::bernoulli_distribution bit(density);
        std::uniform_int_distribution<int> step(1, 2);
        for (int b = 0; b < heads; b++) {
            A(b) = 0.f;
            for (int n = 0; n < seq; n++) {
                Delta(n, b) = (float)step(rng);
                for (int dd = 0; dd < channels; dd++) {
                    X(dd, n, b) = float16_t(bit(rng) ? 1.f : 0.f);
                }
                if (b < groups) {
                    for (int pp = 0; pp < state; pp++) {
                        Bm(pp, n, b) = float16_t(bit(rng) ? 1.f : 0.f);
                        Cm(pp, n, b) = float16_t(bit(rng) ? 1.f : 0.f);
                    }
                }
            }
        }
        run();
        ok &= check(X, Bm, Cm, Delta, A, result, "sparse whole numbers", 0.0);
    }

    // Then the arithmetic, with the decay and the precision the app is for.
    {
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
        run();
        ok &= check(X, Bm, Cm, Delta, A, result, "random", 1e-3);
    }

    // Two multiplies of chunk x state x chunk and chunk x chunk x channels for
    // the scores, and two of state x chunk x channels and chunk x state x
    // channels for the state, per chunk. All but the scores are per head; the
    // scores are per group, because every head of a group has the same ones.
    const double flops =
        2.0 * num_chunks * chunk *
        ((double)groups * chunk * state +
         heads * ((double)chunk * channels + 2.0 * state * channels));
    double t = hb::bench_s([&]() { mamba2(X, Bm, Cm, Delta, A, result); },
                           10, [&]() { result.device_sync(); });
    printf("  Halide mamba2 %30.0f GFlop/s %10.1f us\n", flops / t / 1e9, t * 1e6);

    printf("%s\n", ok ? "Success!" : "FAILED");
    return ok ? 0 : 1;
}
