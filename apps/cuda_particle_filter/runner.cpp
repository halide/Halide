#include "HalideBuffer.h"
#include "particle_filter.h"
#include "../support/bench_harness.h"
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

using namespace Halide::Runtime;

// Draw the true state trajectory and observations from the Gordon model.
static void simulate(int T, int B, Buffer<float> &obs, std::vector<std::vector<float>> &tx) {
    std::mt19937 rng(2024);
    std::normal_distribution<float> nv(0.f, std::sqrt(10.f)), nw(0.f, 1.f), n0(0.f, std::sqrt(5.f));
    for (int b = 0; b < B; b++) {
        float x = n0(rng);
        for (int t = 0; t < T; t++) {
            if (t > 0) x = 0.5f * x + 25.f * x / (1.f + x * x) + 8.f * std::cos(1.2f * (t - 1)) + nv(rng);
            tx[b][t] = x;
            obs(t, b) = x * x / 20.f + nw(rng);
        }
    }
}

// A plain C++ bootstrap filter with systematic resampling: the reference the
// Halide filter should statistically agree with.
static double reference_rmse(int T, int B, int N, const Buffer<float> &obs,
                             const std::vector<std::vector<float>> &tx) {
    std::mt19937 rng(7);
    std::normal_distribution<float> nv(0.f, std::sqrt(10.f)), n0(0.f, std::sqrt(5.f));
    std::uniform_real_distribution<float> uni(0.f, 1.f);
    std::vector<float> x(N), w(N), xn(N);
    double se = 0; long cnt = 0;
    for (int b = 0; b < B; b++) {
        for (int p = 0; p < N; p++) x[p] = n0(rng);
        for (int t = 0; t < T; t++) {
            double sw = 0, swx = 0, mx = -1e30;
            for (int p = 0; p < N; p++) {
                float r = obs(t, b) - x[p] * x[p] / 20.f;
                w[p] = -0.5f * r * r;
                mx = std::max(mx, (double)w[p]);
            }
            for (int p = 0; p < N; p++) { w[p] = std::exp(w[p] - mx); sw += w[p]; swx += w[p] * x[p]; }
            double est = swx / sw;
            if (t >= 10) { double e = est - tx[b][t]; se += e * e; cnt++; }
            // systematic resample
            std::vector<float> c(N); c[0] = w[0]; for (int p = 1; p < N; p++) c[p] = c[p-1] + w[p];
            float u0 = uni(rng) / N * (float)sw; int j = 0;
            for (int p = 0; p < N; p++) {
                float u = u0 + (float)p / N * (float)sw;
                while (j < N - 1 && c[j] < u) j++;
                xn[p] = x[j];
            }
            for (int p = 0; p < N; p++) {
                float d = 0.5f * xn[p] + 25.f * xn[p] / (1.f + xn[p] * xn[p]) + 8.f * std::cos(1.2f * t);
                x[p] = d + nv(rng);
            }
        }
    }
    return std::sqrt(se / cnt);
}

int main() {
    const int T = STEPS, B = BATCH, N = PARTICLES;
    Buffer<float> obs(T, B), est(T, B);
    std::vector<std::vector<float>> tx(B, std::vector<float>(T));
    simulate(T, B, obs, tx);

    est.fill(0.f);
    particle_filter(obs, est);  // warm / JIT-free AOT
    // On a GPU target the result lands device-side; the batched timer syncs
    // each batch and copy_to_host brings it back before the host reads it.
    // Both are no-ops for the CPU target.
    auto body = [&]() { particle_filter(obs, est); };
    auto finish = [&]() { est.device_sync(); };
    hb::Stats s = hb::bench(body, 1, finish);
    est.copy_to_host();

    double se = 0; long cnt = 0; bool finite = true;
    for (int b = 0; b < B; b++)
        for (int t = 10; t < T; t++) {
            if (!std::isfinite(est(t, b))) finite = false;
            double e = est(t, b) - tx[b][t]; se += e * e; cnt++;
        }
    double rmse = std::sqrt(se / cnt);
    double ref = reference_rmse(T, B, N, obs, tx);

    printf("particle filter  %d particles x %d steps x %d filters  %.3f ms\n", N, T, B, s.min);
    printf("  RMSE vs true state: %.3f  (systematic-resampling reference: %.3f)\n", rmse, ref);
    // The Metropolis filter should track the state about as well as the
    // reference; allow a margin for the different resampler and Monte Carlo.
    bool ok = finite && rmse < ref * 1.25 + 1.0;
    printf("  -> %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
