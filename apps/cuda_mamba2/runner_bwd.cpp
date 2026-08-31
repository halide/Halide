// Linked against libHalide only for float16_t, as the forward runner is.
#include "Halide.h"
#include "HalideBuffer.h"
#include "halide_benchmark.h"
#include "mamba2_bwd.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <thread>
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
constexpr int hpg = heads / groups;

// The recurrence differentiated step by step for one head. Stores the state
// at every step on the way forward, then walks back carrying the state's
// gradient. Emits this head's dX and ddt rows, its contribution to its
// group's dB and dC, and its dA term.
static void reference_head_bwd(const Buffer<float16_t> &X, const Buffer<float16_t> &Bm,
                               const Buffer<float16_t> &Cm, const Buffer<float> &Delta,
                               float A, const Buffer<float16_t> &dY,
                               int b, std::vector<float> &Sbuf,
                               std::vector<float> &dXh, std::vector<float> &ddth,
                               std::vector<double> &dBg, std::vector<double> &dCg,
                               double *dAh) {
    const int g = b / hpg;
    // Forward, storing the state after every step.
    std::vector<float> S(state * channels, 0.f);
    for (int n = 0; n < seq; n++) {
        float dt = Delta(n, b);
        float a = std::exp(dt * A);
        for (int pp = 0; pp < state; pp++) {
            float db = dt * (float)Bm(pp, n, g);
            for (int dd = 0; dd < channels; dd++) {
                S[pp * channels + dd] = a * S[pp * channels + dd] + db * (float)X(dd, n, b);
            }
        }
        std::copy(S.begin(), S.end(), Sbuf.begin() + (size_t)n * state * channels);
    }
    // Backward.
    std::vector<float> dS(state * channels, 0.f), carry(state * channels, 0.f);
    double dA = 0;
    for (int n = seq - 1; n >= 0; n--) {
        float dt = Delta(n, b);
        float a = std::exp(dt * A);
        const float *Sn = Sbuf.data() + (size_t)n * state * channels;
        const float *Sp = n > 0 ? Sbuf.data() + (size_t)(n - 1) * state * channels : nullptr;
        double term1 = 0, term2 = 0;
        for (int pp = 0; pp < state; pp++) {
            float c = (float)Cm(pp, n, g);
            double dCacc = 0;
            for (int dd = 0; dd < channels; dd++) {
                float v = c * (float)dY(dd, n % chunk, n / chunk, b) + carry[pp * channels + dd];
                dS[pp * channels + dd] = v;
                dCacc += (double)Sn[pp * channels + dd] * (float)dY(dd, n % chunk, n / chunk, b);
                term1 += (double)v * (float)Bm(pp, n, g) * (float)X(dd, n, b);
                if (Sp) {
                    term2 += (double)v * Sp[pp * channels + dd];
                }
            }
            dCg[((size_t)(n / chunk) * chunk + (n % chunk)) * state + pp] += dCacc;
        }
        for (int dd = 0; dd < channels; dd++) {
            double acc = 0;
            for (int pp = 0; pp < state; pp++) {
                acc += (double)Bm(pp, n, g) * dS[pp * channels + dd];
            }
            dXh[(size_t)n * channels + dd] = (float)(dt * acc);
        }
        for (int pp = 0; pp < state; pp++) {
            double acc = 0;
            for (int dd = 0; dd < channels; dd++) {
                acc += (double)dS[pp * channels + dd] * (float)X(dd, n, b);
            }
            dBg[((size_t)(n / chunk) * chunk + (n % chunk)) * state + pp] += dt * acc;
        }
        ddth[n] = (float)(term1 + (double)A * a * term2);
        dA += dt * a * term2;
        for (int pp = 0; pp < state; pp++) {
            for (int dd = 0; dd < channels; dd++) {
                carry[pp * channels + dd] = a * dS[pp * channels + dd];
            }
        }
    }
    *dAh = dA;
}

struct Refs {
    // Per-head dX and ddt for the checked heads only; group sums over all.
    std::vector<int> check_heads;
    std::vector<std::vector<float>> dX, ddt;
    std::vector<double> dA;                    // all heads
    std::vector<std::vector<double>> dB, dC;   // per group
};

static Refs run_reference(const Buffer<float16_t> &X, const Buffer<float16_t> &Bm,
                          const Buffer<float16_t> &Cm, const Buffer<float> &Delta,
                          const Buffer<float> &A, const Buffer<float16_t> &dY) {
    Refs r;
    r.check_heads = {0, 1, heads / 2, heads - 1};
    std::sort(r.check_heads.begin(), r.check_heads.end());
    r.check_heads.erase(std::unique(r.check_heads.begin(), r.check_heads.end()),
                        r.check_heads.end());
    r.dX.assign(r.check_heads.size(), {});
    r.ddt.assign(r.check_heads.size(), {});
    r.dA.assign(heads, 0.0);
    r.dB.assign(groups, std::vector<double>((size_t)seq * state, 0.0));
    r.dC.assign(groups, std::vector<double>((size_t)seq * state, 0.0));

    // Group sums need every head, so sweep them all, threaded.
    const int nthreads = std::min(8u, std::thread::hardware_concurrency());
    std::vector<std::thread> pool;
    std::vector<std::vector<std::vector<double>>> dBt(nthreads), dCt(nthreads);
    for (int ti = 0; ti < nthreads; ti++) {
        dBt[ti].assign(groups, std::vector<double>((size_t)seq * state, 0.0));
        dCt[ti].assign(groups, std::vector<double>((size_t)seq * state, 0.0));
    }
    std::vector<std::vector<float>> dXall(heads), ddtall(heads);
    for (int ti = 0; ti < nthreads; ti++) {
        pool.emplace_back([&, ti]() {
            std::vector<float> Sbuf((size_t)seq * state * channels);
            std::vector<float> dXh((size_t)seq * channels), ddth(seq);
            for (int b = ti; b < heads; b += nthreads) {
                double dA = 0;
                reference_head_bwd(X, Bm, Cm, Delta, A(b), dY, b, Sbuf,
                                   dXh, ddth, dBt[ti][b / hpg], dCt[ti][b / hpg], &dA);
                r.dA[b] = dA;
                for (int ci = 0; ci < (int)r.check_heads.size(); ci++) {
                    if (r.check_heads[ci] == b) {
                        r.dX[ci] = dXh;
                        r.ddt[ci] = ddth;
                    }
                }
            }
        });
    }
    for (auto &th : pool) {
        th.join();
    }
    for (int ti = 0; ti < nthreads; ti++) {
        for (int gg = 0; gg < groups; gg++) {
            for (size_t z = 0; z < r.dB[gg].size(); z++) {
                r.dB[gg][z] += dBt[ti][gg][z];
                r.dC[gg][z] += dCt[ti][gg][z];
            }
        }
    }
    return r;
}

static bool report(const char *what, double max_err, double max_mag, double rel_tol) {
    bool ok = max_err <= rel_tol * std::max(1.0, max_mag);
    printf("%-24s max error %.3e against a magnitude of %.3e: %s\n",
           what, max_err, max_mag, ok ? "ok" : "FAILED");
    if (rel_tol == 0.0 && max_mag >= 2048) {
        printf("  the answers have grown past what half precision holds exactly, "
               "so this check is no longer one\n");
        ok = false;
    }
    return ok;
}

static bool check_all(const Refs &r,
                      const Buffer<float16_t> &dX, const Buffer<float16_t> &dB,
                      const Buffer<float16_t> &dC, const Buffer<float> &dDT,
                      const Buffer<float> &dA, double rel_tol, double loose_tol,
                      double dA_tol) {
    bool ok = true;
    int reported = 0;
    {
        double err = 0, mag = 0;
        for (int ci = 0; ci < (int)r.check_heads.size(); ci++) {
            int b = r.check_heads[ci];
            for (int n = 0; n < seq; n++) {
                for (int dd = 0; dd < channels; dd++) {
                    double want = r.dX[ci][(size_t)n * channels + dd];
                    double got = (float)dX(dd, n % chunk, n / chunk, b);
                    double e = std::abs(got - want);
                    if (e > rel_tol * std::max(1.0, std::abs(want)) && reported < 5) {
                        printf("  dX head %d n %d d %d: %f vs %f\n", b, n, dd, got, want);
                        reported++;
                    }
                    err = std::max(err, e);
                    mag = std::max(mag, std::abs(want));
                }
            }
        }
        ok &= report("dX", err, mag, rel_tol);
    }
    {
        double errB = 0, magB = 0, errC = 0, magC = 0;
        for (int gg = 0; gg < groups; gg++) {
            for (int n = 0; n < seq; n++) {
                for (int pp = 0; pp < state; pp++) {
                    double wantB = r.dB[gg][(size_t)n * state + pp];
                    double gotB = (float)dB(pp, n % chunk, n / chunk, gg);
                    double wantC = r.dC[gg][(size_t)n * state + pp];
                    double gotC = (float)dC(pp, n % chunk, n / chunk, gg);
                    if (std::abs(gotB - wantB) > rel_tol * std::max(1.0, std::abs(wantB)) && reported < 10) {
                        printf("  dB group %d n %d p %d: %f vs %f\n", gg, n, pp, gotB, wantB);
                        reported++;
                    }
                    if (std::abs(gotC - wantC) > rel_tol * std::max(1.0, std::abs(wantC)) && reported < 10) {
                        printf("  dC group %d n %d p %d: %f vs %f\n", gg, n, pp, gotC, wantC);
                        reported++;
                    }
                    errB = std::max(errB, std::abs(gotB - wantB));
                    magB = std::max(magB, std::abs(wantB));
                    errC = std::max(errC, std::abs(gotC - wantC));
                    magC = std::max(magC, std::abs(wantC));
                }
            }
        }
        ok &= report("dB", errB, magB, rel_tol);
        ok &= report("dC", errC, magC, rel_tol);
    }
    {
        double err = 0, mag = 0;
        for (int ci = 0; ci < (int)r.check_heads.size(); ci++) {
            int b = r.check_heads[ci];
            for (int n = 0; n < seq; n++) {
                double want = r.ddt[ci][n];
                double got = dDT(n, b);
                if (std::abs(got - want) > loose_tol * std::max(1.0, std::abs(want)) && reported < 15) {
                    printf("  ddt head %d n %d: %f vs %f\n", b, n, got, want);
                    reported++;
                }
                err = std::max(err, std::abs(got - want));
                mag = std::max(mag, std::abs(want));
            }
        }
        ok &= report("ddt", err, mag, loose_tol);
    }
    {
        double err = 0, mag = 0;
        for (int b = 0; b < heads; b++) {
            err = std::max(err, std::abs((double)dA(b) - r.dA[b]));
            mag = std::max(mag, std::abs(r.dA[b]));
        }
        // dA is a sequence-length sum whose terms carry the half precision
        // rounding of Y and dX, against an f64 reference.
        ok &= report("dA", err, mag, dA_tol);
    }
    return ok;
}

int main(int argc, char **argv) {
    Buffer<float16_t> X(channels, seq, heads), Bm(state, seq, groups), Cm(state, seq, groups);
    Buffer<float> Delta(seq, heads), A(heads);
    Buffer<float16_t> Y(channels, chunk, num_chunks, heads);
    Buffer<float16_t> dY(channels, chunk, num_chunks, heads);
    Buffer<float16_t> dX(channels, chunk, num_chunks, heads);
    Buffer<float16_t> dB(state, chunk, num_chunks, groups);
    Buffer<float16_t> dC(state, chunk, num_chunks, groups);
    Buffer<float> dDT(seq, heads), dA(heads);

    // The forward output, computed the slow exact way for the checked
    // configuration (the generator only needs it for the ddt identity, and
    // the identity is checked through ddt itself).
    auto forward_y = [&]() {
        const int nthreads = std::min(8u, std::thread::hardware_concurrency());
        std::vector<std::thread> pool;
        for (int ti = 0; ti < nthreads; ti++) {
            pool.emplace_back([&, ti]() {
                std::vector<float> S(state * channels);
                for (int b = ti; b < heads; b += nthreads) {
                    const int g = b / hpg;
                    std::fill(S.begin(), S.end(), 0.f);
                    for (int n = 0; n < seq; n++) {
                        float dt = Delta(n, b);
                        float a = std::exp(dt * A(b));
                        for (int pp = 0; pp < state; pp++) {
                            float db = dt * (float)Bm(pp, n, g);
                            for (int dd = 0; dd < channels; dd++) {
                                S[pp * channels + dd] = a * S[pp * channels + dd] + db * (float)X(dd, n, b);
                            }
                        }
                        for (int dd = 0; dd < channels; dd++) {
                            float y = 0;
                            for (int pp = 0; pp < state; pp++) {
                                y += (float)Cm(pp, n, g) * S[pp * channels + dd];
                            }
                            Y(dd, n % chunk, n / chunk, b) = (float16_t)y;
                        }
                    }
                }
            });
        }
        for (auto &th : pool) {
            th.join();
        }
    };

    auto run = [&]() {
        if (mamba2_bwd(X, Bm, Cm, Delta, A, Y, dY, dX, dB, dC, dDT, dA) != 0) {
            printf("mamba2_bwd returned an error\n");
            exit(1);
        }
        dX.copy_to_host();
        dB.copy_to_host();
        dC.copy_to_host();
        dDT.copy_to_host();
        dA.copy_to_host();
    };

    bool ok = true;

    // The structural check: no decay, whole numbers, sparse enough that
    // nothing overflows what the output types hold exactly. The group sums
    // add a factor of the heads in a group, so the inputs are sparser than
    // the forward test's.
    {
        std::mt19937 rng(7);
        const double density =
            std::cbrt(24.0 / (1.5 * (double)state * (double)seq)) / std::cbrt((double)hpg);
        std::bernoulli_distribution bit(density);
        std::uniform_int_distribution<int> step(1, 2);
        for (int b = 0; b < heads; b++) {
            A(b) = 0.f;
            for (int n = 0; n < seq; n++) {
                Delta(n, b) = (float)step(rng);
                for (int dd = 0; dd < channels; dd++) {
                    X(dd, n, b) = float16_t(bit(rng) ? 1.f : 0.f);
                    dY(dd, n % chunk, n / chunk, b) = float16_t(bit(rng) ? 1.f : 0.f);
                }
                if (b < groups) {
                    for (int pp = 0; pp < state; pp++) {
                        Bm(pp, n, b) = float16_t(bit(rng) ? 1.f : 0.f);
                        Cm(pp, n, b) = float16_t(bit(rng) ? 1.f : 0.f);
                    }
                }
            }
        }
        forward_y();
        run();
        printf("sparse whole numbers:\n");
        Refs r = run_reference(X, Bm, Cm, Delta, A, dY);
        // dA is a large sum of whole numbers that can pass what f32 holds
        // exactly, so it gets a tolerance even here.
        ok &= check_all(r, dX, dB, dC, dDT, dA, 0.0, 1e-5, 1e-5);
    }

    // The arithmetic, with decay, at the precision the pass is for.
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
                    dY(dd, n % chunk, n / chunk, b) = float16_t(dist(rng));
                }
                if (b < groups) {
                    for (int pp = 0; pp < state; pp++) {
                        Bm(pp, n, b) = float16_t(dist(rng));
                        Cm(pp, n, b) = float16_t(dist(rng));
                    }
                }
            }
        }
        forward_y();
        run();
        printf("random:\n");
        Refs r = run_reference(X, Bm, Cm, Delta, A, dY);
        ok &= check_all(r, dX, dB, dC, dDT, dA, 4e-3, 2e-2, 1e-1);
    }

    // Matrix multiply flops only: the two recomputed forward multiplies and
    // qk, the gradient-state multiply, the three multiplies against dY per
    // head, the two group-summed multiplies per head, and the two state
    // gradient consumers.
    const double flops =
        2.0 * num_chunks *
        ((double)groups * chunk * chunk * state +
         (double)heads * (3.0 * chunk * chunk * channels +
                          2.0 * chunk * chunk * state +
                          5.0 * chunk * state * channels));
    double tm = Halide::Tools::benchmark(3, 3, [&]() {
        mamba2_bwd(X, Bm, Cm, Delta, A, Y, dY, dX, dB, dC, dDT, dA);
        dA.device_sync();
    });
    printf("  Halide mamba2 bwd %26.0f GFlop/s %10.1f us\n", flops / tm / 1e9, tm * 1e6);

    printf("%s\n", ok ? "Success!" : "FAILED");
    return ok ? 0 : 1;
}
