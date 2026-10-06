#include "harness.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numeric>
#include <random>

namespace gq {
namespace {

double now_ns() {
    return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

double time_ns(Kernel *k, int reps) {
    double t0 = now_ns();
    k->run(reps);
    return (now_ns() - t0) / ((double)reps * k->batch);
}

}  // namespace

// Methodology: per kernel, calibrate reps so one sample takes >= min_sample_ms,
// then warm up for >= 3 samples. Then `rounds` rounds; each round times one
// sample of every kernel, in a fresh random order, so that drift (thermal,
// frequency, other load) hits all kernels alike. Report the median and a
// distribution-free 95% CI of the median (order statistics, binomial).
std::vector<Timing> time_interleaved(const std::vector<Kernel *> &ks, int rounds, double min_sample_ms) {
    std::vector<int> reps(ks.size(), 1);
    for (size_t i = 0; i < ks.size(); i++) {
        double t = time_ns(ks[i], 1) * ks[i]->batch;  // per rep
        while (t * reps[i] < min_sample_ms * 1e6 && reps[i] < (1 << 24)) {
            reps[i] = std::max(reps[i] * 2, (int)std::ceil(min_sample_ms * 1e6 / std::max(t, 1.0)));
            t = time_ns(ks[i], reps[i]) * ks[i]->batch;
        }
        for (int w = 0; w < 3; w++)
            time_ns(ks[i], reps[i]);
    }
    std::vector<std::vector<double>> s(ks.size());
    std::vector<int> order(ks.size());
    std::iota(order.begin(), order.end(), 0);
    std::mt19937 rng(1);
    for (int r = 0; r < rounds; r++) {
        std::shuffle(order.begin(), order.end(), rng);
        for (int i : order)
            s[i].push_back(time_ns(ks[i], reps[i]));
    }
    std::vector<Timing> out;
    for (size_t i = 0; i < ks.size(); i++) {
        auto &v = s[i];
        std::sort(v.begin(), v.end());
        int n = v.size();
        double half = 0.98 * std::sqrt((double)n);  // 1.96 * sqrt(n) / 2
        int lo = std::max(0, (int)std::floor(n / 2.0 - half));
        int hi = std::min(n - 1, (int)std::ceil(n / 2.0 + half));
        double med = n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2;
        out.push_back({med, v[lo], v[hi], n, reps[i]});
    }
    return out;
}

}  // namespace gq
