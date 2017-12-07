#ifndef BENCHMARK_H
#define BENCHMARK_H

#include <algorithm>
#include <cassert>
#include <chrono>
#include <functional>
#include <limits>

namespace Halide {
namespace Tools {

// Prefer high_resolution_clock, but only if it's steady...
template <bool HighResIsSteady = std::chrono::high_resolution_clock::is_steady>
struct SteadyClock {
    using type = std::chrono::high_resolution_clock;
};

// ...otherwise use steady_clock.
template <>
struct SteadyClock<false> {
    using type = std::chrono::steady_clock;
};

// Benchmark the operation 'op'. The number of iterations refers to
// how many times the operation is run for each time measurement, the
// result is the minimum over a number of samples runs. The result is the
// amount of time in seconds for one iteration.
//
// NOTE: it is usually simpler and more accurate to use the adaptive
// version of benchmark() later in this file; this function is provided
// for legacy code.
//
// IMPORTANT NOTE: Using this tool for timing GPU code may be misleading,
// as it does not account for time needed to synchronize to/from the GPU;
// if the callback doesn't include calls to device_sync(), the reported
// time may only be that to queue the requests; if the callback *does*
// include calls to device_sync(), it might exaggerate the sync overhead
// for real-world use. For now, callers using this to benchmark GPU
// code should measure with extreme caution.

inline double benchmark(int samples, int iterations, std::function<void()> op) {
    using BenchmarkClock = SteadyClock<>::type;
    double best = std::numeric_limits<double>::infinity();
    for (int i = 0; i < samples; i++) {
        auto start = BenchmarkClock::now();
        for (int j = 0; j < iterations; j++) {
            op();
        }
        auto end = BenchmarkClock::now();
        double elapsed_seconds =
                std::chrono::duration_cast<std::chrono::duration<double>>(end - start).count();
        best = std::min(best, elapsed_seconds);
    }
    return best / iterations;
}

// Benchmark the operation 'op': run the operation until at least min_time
// has elapsed, with the constraint of at least min_iters and no more than
// max_iters times; the number of iterations is expanded as we
// progress (based on initial runs of 'op') to minimize overhead. The time
// reported will be that of the best single iteration.
//
// Most callers should be able to get good results without needing to specify
// custom BenchmarkConfig values.
//
// IMPORTANT NOTE: Using this tool for timing GPU code may be misleading,
// as it does not account for time needed to synchronize to/from the GPU;
// if the callback doesn't include calls to device_sync(), the reported
// time may only be that to queue the requests; if the callback *does*
// include calls to device_sync(), it might exaggerate the sync overhead
// for real-world use. For now, callers using this to benchmark GPU
// code should measure with extreme caution.

constexpr uint64_t kBenchmarkMaxIterations = 1000000000;

struct BenchmarkConfig {
    // Attempt to use this much time (in seconds) for the meaningful samples
    // taken; initial iterations will be done to find an iterations-per-sample
    // count that puts the total runtime in this ballpark.
    double min_time{0.1};

    // Set an absolute upper time limit. Defaults to min_time * 4.
    double max_time{0.1 * 4};

    // Run at least this many iterations per sample.
    uint64_t min_iters{1};

    // Run at most this many iterations over all samples.
    uint64_t max_iters{kBenchmarkMaxIterations};

    // Terminate when the relative difference between the best runtime
    // seen and the third-best runtime seen is no more than
    // this. Controls accuracy. The closer to zero this gets the more
    // reliable the answer, but the longer it may take to run.
    double accuracy{0.03};
};

struct BenchmarkResult {
    // Best elapsed wall-clock time per iteration (seconds).
    double wall_time;

    // Number of samples used for measurement.
    // (There might be additional samples taken that are not used
    // for measurement.)
    uint64_t samples;

    // Total number of iterations across all samples.
    // (There might be additional iterations taken that are not used
    // for measurement.)
    uint64_t iterations;

    // Measured accuracy between the best and third-best result.
    // Will be <= config.accuracy unless max_iters is exceeded.
    double accuracy;

    operator double() const { return wall_time; }
};

inline BenchmarkResult benchmark(std::function<void()> op, const BenchmarkConfig& config = {}) {
    BenchmarkResult result{0, 0, 0};

    const double min_time = std::max(10 * 1e-6, config.min_time);
    const double max_time = std::max(config.min_time, config.max_time);

    const uint64_t min_iters = std::min(std::max((uint64_t)1, config.min_iters),
                                                                            kBenchmarkMaxIterations);
    const uint64_t max_iters = std::min(
            std::max(config.min_iters, config.max_iters), kBenchmarkMaxIterations);
    const double accuracy = 1.0 + std::min(std::max(0.001, config.accuracy), 0.1);

    // We will do (at least) kMinSamples samples; we will do additional
    // samples until the best the kMinSamples'th results are within the
    // accuracy tolerance (or we run out of iterations).
    constexpr int kMinSamples = 3;
    double times[kMinSamples + 1] = {0};

    double total_time = 0;
    uint64_t iters_per_sample = min_iters;
    while (result.iterations < max_iters) {
        result.samples = 0;
        result.iterations = 0;
        total_time = 0;
        for (int i = 0; i < kMinSamples; i++) {
            times[i] = benchmark(1, iters_per_sample, op);
            result.samples++;
            result.iterations += iters_per_sample;
            total_time += times[i] * iters_per_sample;
        }
        std::sort(times, times + kMinSamples);
        if (times[0] * iters_per_sample * kMinSamples >= min_time) {
            break;
        }
        // Use an estimate based on initial times to converge faster.
        double next_iters = std::max(min_time / std::max(times[0] * kMinSamples, 1e-9),
                                                                 iters_per_sample * 2.0);
        iters_per_sample = (uint64_t)(next_iters + 0.5);
    }

    // - Keep taking samples until we are accurate enough (even if we run over min_time).
    // - If we are already accurate enough but have time remaining, keep taking samples.
    // - No matter what, don't go over max_iters or max_time; this is important, in case
    // we happen to get faster results for the first samples, then happen to transition
    // to throttled-down CPU state.
    while ((times[0] * accuracy < times[kMinSamples - 1] || total_time < min_time) &&
                 total_time < max_time &&
                 result.iterations < max_iters) {
        times[kMinSamples] = benchmark(1, iters_per_sample, op);
        result.samples++;
        result.iterations += iters_per_sample;
        total_time += times[kMinSamples] * iters_per_sample;
        std::sort(times, times + kMinSamples + 1);
    }
    result.wall_time = times[0];
    result.accuracy = (times[kMinSamples - 1] / times[0]) - 1.0;

    return result;
}

}   // namespace Tools
}   // mamespace Halide

#endif
