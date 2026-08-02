#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <vector>

struct TimingResult {
    double min_ns = 0.0;
    double median_ns = 0.0;
};

// Runs `fn` `warmup` times (discarded), then calibrates a batch size large
// enough that timing a whole batch back-to-back amortizes clock overhead and
// resolution (individual quantize_row/vec_dot calls on fast SIMD kernels can
// complete in a few nanoseconds -- timing them one at a time, even with a
// high-resolution clock, is dominated by noise), then times `iters` such
// batches and returns the min/median per-call latency in nanoseconds.
inline TimingResult time_calls(const std::function<void()> &fn, int warmup = 5, int iters = 20) {
    using clock = std::chrono::steady_clock;

    for (int i = 0; i < warmup; ++i) {
        fn();
    }

    constexpr double kMinBatchNs = 200000.0;  // 0.2ms per batch
    int batch = 1;
    for (;;) {
        const auto t0 = clock::now();
        for (int i = 0; i < batch; ++i) {
            fn();
        }
        const auto t1 = clock::now();
        const double batch_ns = std::chrono::duration<double, std::nano>(t1 - t0).count();
        if (batch_ns >= kMinBatchNs || batch >= (1 << 20)) {
            break;
        }
        batch *= 4;
    }

    std::vector<double> samples_ns;
    samples_ns.reserve(iters);
    for (int i = 0; i < iters; ++i) {
        const auto t0 = clock::now();
        for (int b = 0; b < batch; ++b) {
            fn();
        }
        const auto t1 = clock::now();
        const double batch_ns = std::chrono::duration<double, std::nano>(t1 - t0).count();
        samples_ns.push_back(batch_ns / batch);
    }

    std::sort(samples_ns.begin(), samples_ns.end());
    TimingResult result;
    result.min_ns = samples_ns.front();
    result.median_ns = samples_ns[samples_ns.size() / 2];
    return result;
}

inline double bytes_per_sec(size_t bytes, double ns) {
    if (ns <= 0.0) {
        return 0.0;
    }
    return static_cast<double>(bytes) / (ns * 1e-9);
}

inline double gflops(double flops, double ns) {
    if (ns <= 0.0) {
        return 0.0;
    }
    return flops / (ns * 1e-9) / 1e9;
}
