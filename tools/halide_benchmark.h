#ifndef BENCHMARK_H
#define BENCHMARK_H

#include <algorithm>
#include <cassert>
#include <chrono>
#include <functional>
#include <limits>

namespace Halide {
namespace Tools {

// TODO(srj): mark this as deprecated once existing usages are converted.
// HALIDE_ATTRIBUTE_DEPRECATED("benchmark() with explicit samples-and-iterations is deprecated.") 
double benchmark(int samples, int iterations, std::function<void()> op) {
    double best = std::numeric_limits<double>::infinity();
    for (int i = 0; i < samples; i++) {
        auto t1 = std::chrono::high_resolution_clock::now();
        for (int j = 0; j < iterations; j++) {
            op();
        }
        auto t2 = std::chrono::high_resolution_clock::now();
        double dt = std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count() / 1e6;
        if (dt < best) best = dt;
    }
    return best / iterations;
}



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

// Benchmark the operation 'op': run the operation until at least min_time
// has elapsed, with the constraint of at least min_iters and no more than
// max_iters times; the number of iterations is expanded as we
// progress (based on initial runs of 'op') to minimize overhead. Most
// callers should be able to get good results without needing to specify
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
    // Attempt to run a benchmark for at least this long (seconds).
    double min_time;
    
    // Run at least this many iterations.
    uint64_t min_iters;

    // Run at most this many iterations.
    uint64_t max_iters;

    BenchmarkConfig() : min_time(0.5), min_iters(1), max_iters(kBenchmarkMaxIterations) {}
    explicit BenchmarkConfig(double min_time) : min_time(min_time), min_iters(1), max_iters(kBenchmarkMaxIterations) {}
    BenchmarkConfig(double min_time, uint64_t min_iters, uint64_t max_iters) : min_time(min_time), min_iters(min_iters), max_iters(max_iters) {}
};

struct BenchmarkResult {
    // Average elapsed wall-clock time per iteration (seconds).
    double wall_time;

    // Actual number of iterations.
    uint64_t iterations;
};

BenchmarkResult benchmark(std::function<void()> op, const BenchmarkConfig& config = BenchmarkConfig()) {
    using BenchmarkClock = SteadyClock<>::type;

    const double min_time = std::max(0.01, config.min_time);
    const uint64_t min_iters = std::min(std::max((uint64_t) 1, config.min_iters), kBenchmarkMaxIterations);
    const uint64_t max_iters = std::min(std::max(config.min_iters, config.max_iters), kBenchmarkMaxIterations);

    uint64_t iterations_total = 0;
    double elapsed_total = 0.0;
    uint64_t iters = min_iters;
    for (;;) {
        auto start = BenchmarkClock::now();
        for (uint64_t i = 0; i < iters; i++) {
            op();
        }
        auto end = BenchmarkClock::now();
        double elapsed_seconds = std::chrono::duration_cast<std::chrono::duration<double>>(end - start).count();
        iterations_total += iters;
        elapsed_total += elapsed_seconds;
        if (elapsed_seconds >= min_time ||
            iterations_total >= max_iters) {
            return { elapsed_total / iterations_total, iterations_total };
        }

        // Gradually enlarge iters as we go along.
        double scale = (min_time * 1.4142) / std::max(elapsed_seconds, 1e-9);
        double next_iters = std::min(std::max(scale * iters, iters + 1.0), (double) kBenchmarkMaxIterations);
        iters = (uint64_t) (next_iters + 0.5);
    }

    // Should be unreachable.
    return { 0, 0 };
}

}   // namespace Tools
}   // mamespace Halide

#endif
