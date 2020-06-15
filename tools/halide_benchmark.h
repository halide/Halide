#ifndef BENCHMARK_H
#define BENCHMARK_H

#include <algorithm>
#include <cassert>
#include <chrono>
#include <functional>
#include <limits>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

namespace Halide {
namespace Tools {

#if !defined(__EMSCRIPTEN__)

// Prefer high_resolution_clock, but only if it's steady...
template<bool HighResIsSteady = std::chrono::high_resolution_clock::is_steady>
struct SteadyClock {
    using type = std::chrono::high_resolution_clock;
};

// ...otherwise use steady_clock.
template<>
struct SteadyClock<false> {
    using type = std::chrono::steady_clock;
};

inline SteadyClock<>::type::time_point benchmark_now() {
    return SteadyClock<>::type::now();
}

inline double benchmark_duration_seconds(
    SteadyClock<>::type::time_point start,
    SteadyClock<>::type::time_point end) {
    return std::chrono::duration_cast<std::chrono::duration<double>>(end - start).count();
}

#else  // __EMSCRIPTEN__

// Emscripten's std::chrono::steady_clock and/or high_resolution_clock
// can throw an exception (!) if the runtime doesn't have a truly
// steady clock available. Advice from emscripten-discuss suggested
// that emscripten_get_now() is the best bet, as it is milliseconds
// (but returned as a double, with microseconds in the fractional portion),
// using either performance.now() or performance.hrtime() depending on the
// environment.
inline double benchmark_now() {
    return emscripten_get_now();
}

inline double benchmark_duration_seconds(double start, double end) {
    return (end - start) / 1000.0;
}

#endif

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

inline double benchmark(uint64_t samples, uint64_t iterations, const std::function<void()> &op) {
    double best = std::numeric_limits<double>::infinity();
    for (uint64_t i = 0; i < samples; i++) {
        auto start = benchmark_now();
        for (uint64_t j = 0; j < iterations; j++) {
            op();
        }
        auto end = benchmark_now();
        double elapsed_seconds = benchmark_duration_seconds(start, end);
        best = std::min(best, elapsed_seconds);
    }
    return best / iterations;
}

// Benchmark the operation 'op': run the operation until at least min_time
// has elapsed; the number of iterations is expanded as we
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
    // Will be <= config.accuracy unless max_time is exceeded.
    double accuracy;

    operator double() const {
        return wall_time;
    }
};

inline BenchmarkResult benchmark(const std::function<void()> &op, const BenchmarkConfig &config = {}) {
    BenchmarkResult result{0, 0, 0};

    const double min_time = std::max(10 * 1e-6, config.min_time);
    const double max_time = std::max(config.min_time, config.max_time);

    const double accuracy = 1.0 + std::min(std::max(0.001, config.accuracy), 0.1);

    // We will do (at least) kMinSamples samples; we will do additional
    // samples until the best the kMinSamples'th results are within the
    // accuracy tolerance (or we run out of iterations).
    constexpr int kMinSamples = 3;
    double times[kMinSamples + 1] = {0};

    double total_time = 0;
    uint64_t iters_per_sample = 1;
    for (;;) {
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
    // - No matter what, don't go over max_time; this is important, in case
    // we happen to get faster results for the first samples, then happen to transition
    // to throttled-down CPU state.
    while ((times[0] * accuracy < times[kMinSamples - 1] || total_time < min_time) &&
           total_time < max_time) {
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

}  // namespace Tools
}  // namespace Halide

#endif
