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
//
// Early stop (side non-empty): after early_min_rounds, stop once, for every
// pair of an ours kernel (side 1) and a GGML one (side 0), the 95% CI of the
// median per-round ratio GGML/ours lies wholly outside the band [lo, hi]:
// the case is clearly a match or clearly not, whatever the remaining rounds
// would add. Cases near parity run every round.
namespace {

constexpr int early_min_rounds = 5;

// 95% CI of the median of v (sorted in place): indices into v.
std::pair<int, int> median_ci(std::vector<double> &v) {
    std::sort(v.begin(), v.end());
    int n = v.size();
    double half = 0.98 * std::sqrt((double)n);  // 1.96 * sqrt(n) / 2
    return {std::max(0, (int)std::floor(n / 2.0 - half)), std::min(n - 1, (int)std::ceil(n / 2.0 + half))};
}

bool decided(const std::vector<std::vector<double>> &s, const std::vector<int> &side, std::pair<double, double> band) {
    for (size_t h = 0; h < s.size(); h++) {
        if (side[h] != 1) continue;
        for (size_t g = 0; g < s.size(); g++) {
            if (side[g] != 0) continue;
            std::vector<double> r(s[h].size());
            for (size_t i = 0; i < r.size(); i++)
                r[i] = s[g][i] / s[h][i];
            auto [lo, hi] = median_ci(r);
            if (r[hi] >= band.first && r[lo] <= band.second) return false;
        }
    }
    return true;
}

}  // namespace

std::vector<Timing> time_interleaved(const std::vector<Kernel *> &ks, int rounds, double min_sample_ms, const std::vector<int> &side, std::pair<double, double> band) {
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
        if (!side.empty() && r + 1 >= early_min_rounds && decided(s, side, band)) break;
    }
    std::vector<Timing> out;
    for (size_t i = 0; i < ks.size(); i++) {
        auto &v = s[i];
        auto [lo, hi] = median_ci(v);
        int n = v.size();
        double med = n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2;
        out.push_back({med, v[lo], v[hi], n, reps[i]});
    }
    return out;
}

}  // namespace gq
