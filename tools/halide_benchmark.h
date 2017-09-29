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
// result is the minimum over a number of samples runs. The result is
// the amount of time in seconds for one iteration.
template<typename Op>
double benchmark(int samples, int iterations, Op op) {
    using Clock = SteadyClock<>::type;
    double best = std::numeric_limits<double>::infinity();
    for (int i = 0; i < samples; i++) {
        auto t1 = Clock::now();
        for (int j = 0; j < iterations; j++) {
            op();
        }
        auto t2 = Clock::now();
        double dt = std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count() / 1e6;
        if (dt < best) best = dt;
    }
    return best / iterations;
}

// Benchmark the operation 'op' using automatically-determined values
// for the number of iterations and the number of samples. Starting at
// config.min_iters, increases iteration count until one sample takes
// at least config.min_time, and increases sample count until the
// relative difference between the best runtime seen and the
// third-best runtime seen is at most config.accuracy. Terminates
// early if the total iteration count exceeds max_iters.
//
// IMPORTANT NOTE: Using this tool for timing GPU code may be misleading,
// as it does not account for time needed to synchronize to/from the GPU;
// if the callback doesn't include calls to device_sync(), the reported
// time may only be that to queue the requests; if the callback *does*
// include calls to device_sync(), it might exaggerate the sync overhead
// for real-world use. For now, callers using this to benchmark GPU
// code should measure with extreme caution.

struct BenchmarkConfig {
    // Terminate when the relative difference between the best runtime
    // seen and the third-best runtime seen is no more than
    // this. Controls accuracy. The closer to zero this gets the more
    // reliable the answer, but the longer it may take to run.
    double accuracy {0.03};
    
    // Take at least this much time per sample (seconds). Prevents
    // benchmarking overhead and clock resolution being a major factor
    // for very cheap benchmarks.
    double min_time {0.005};

    // Run at least this many iterations per sample.
    uint64_t min_iters {1};

    // Run at most this many iterations over all samples.
    uint64_t max_iters {1000};
};

struct BenchmarkResult {
    // Average elapsed wall-clock time per iteration (seconds).
    double wall_time;

    // Actual number of iterations per sample.
    uint64_t iterations;

    // Number of samples taken.
    uint64_t samples;

    operator double() const {return wall_time;}
};

template<typename Op>
BenchmarkResult benchmark(Op op,
                          const BenchmarkConfig& config = BenchmarkConfig()) {
    BenchmarkResult result {0, 0, 0};

    uint64_t total_iters = 0;
    double times[] = {0, 0, 0, 0};
    for (uint64_t iters_per_sample = config.min_iters;
         total_iters < config.max_iters;
         iters_per_sample *= 2) {
        times[0] = benchmark(1, iters_per_sample, op);
        times[1] = benchmark(1, iters_per_sample, op);
        times[2] = benchmark(1, iters_per_sample, op);
        total_iters += 3;
        result.samples = 3;
        std::sort(times, times + 3);
        if (times[0] * iters_per_sample < config.min_time) {
            continue;
        }
        while (times[0] * (1 + config.accuracy) < times[2] && total_iters < config.max_iters) {
            times[3] = benchmark(1, iters_per_sample, op);
            result.samples++;
            total_iters++;
            std::sort(times, times + 4);
        }        
        result.wall_time = times[0];
        result.iterations = iters_per_sample;        
        break;
    }

    return result;
}

}   // namespace Tools
}   // mamespace Halide

#endif
