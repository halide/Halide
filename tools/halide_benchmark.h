#ifndef BENCHMARK_H
#define BENCHMARK_H

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <functional>
#include <iterator>
#include <limits>
#include <numeric>
#include <tuple>
#include <utility>
#include <vector>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

namespace Halide {
namespace Tools {

#if !(defined(__EMSCRIPTEN__) && defined(HALIDE_BENCHMARK_USE_EMSCRIPTEN_GET_NOW))

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
// environment. Unfortunately, it's not guaranteed to be steady, and the
// auto-benchmark algorithm reacts badly to negative times, so we'll leave this
// disabled for now; you can opt-in to this by defining HALIDE_BENCHMARK_USE_EMSCRIPTEN_GET_NOW
// if you need to build for a wasm runtime with the exception behavior above.
inline double benchmark_now() {
    return emscripten_get_now();
}

inline double benchmark_duration_seconds(double start, double end) {
    // emscripten_get_now is *not* guaranteed to be steady.
    // Clamping to a positive value is arguably better than nothing,
    // but still produces unpredictable results in the adaptive case
    // (which is why this is disabled by default).
    return std::max((end - start) / 1000.0, 1e-9);
}

#endif

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

namespace BenchmarkInternal {

// Run 'op' 'iterations' times back-to-back and return the average
// per-iteration wall-clock time (in seconds).
inline double time_iterations(const std::function<void()> &op, uint64_t iterations) {
    auto start = benchmark_now();
    for (uint64_t j = 0; j < iterations; j++) {
        op();
    }
    auto end = benchmark_now();
    return benchmark_duration_seconds(start, end) / iterations;
}

inline void warmup(const std::function<void()> &op, double warmup_time) {
    if (warmup_time <= 0) {
        return;
    }
    auto start = benchmark_now();
    do {
        op();
    } while (benchmark_duration_seconds(start, benchmark_now()) < warmup_time);
}

}  // namespace BenchmarkInternal

struct BenchmarkConfig {
    // Run the operation, discarding the timing, for at least this long
    // (in seconds) before any measured samples are taken. This gives the
    // op a chance to fault in any memory it touches and gives the CPU a
    // chance to leave any idle/low-clock state. Set to 0 to disable.
    double warmup_time{0.01};

    // Attempt to use this much time (in seconds) for the meaningful samples
    // taken; initial iterations will be done to find an iterations-per-sample
    // count that puts the total runtime in this ballpark.
    double min_time{0.1};

    // Set an absolute upper time limit. Defaults to min_time * 4.
    double max_time{0.1 * 4};

    // Maximum value for the computed iters-per-sample.
    // We need this for degenerate cases in which we have a
    // very coarse-grained timer (e.g. some Emscripten/browser environments),
    // and a very short operation being benchmarked; in these cases,
    // we can get timings that are effectively zero, which can explode
    // the predicted next-iter into ~100B or more. It should be unusual
    // that client code needs to adjust this value.
    uint64_t max_iters_per_sample{1000000};

    // Controls the stopping accuracy. benchmark() compares the best and
    // third-best runtimes; benchmark_comparison() uses the multiplicative
    // half-width of paired 95% confidence intervals. The closer to zero this
    // gets the more reliable the answer, but the longer it may take to run.
    double accuracy{0.03};

    // Only used by benchmark_comparison(). The minimum number of complete
    // interleaved measurement blocks. More blocks may be taken, up to max_time,
    // if the paired confidence intervals have not converged to accuracy.
    uint64_t comparison_rounds{20};

    // Target duration of one timed batch in benchmark_comparison(). Short
    // batches allow frequency and thermal drift to cancel within each block.
    // The actual target may be reduced to fit comparison_rounds into min_time.
    double comparison_sample_time{0.005};

    // Seed used to randomize the order of the rows in each balanced comparison
    // design. A fixed default makes benchmark runs reproducible.
    uint64_t comparison_seed{0x6a09e667f3bcc909ULL};
};

struct BenchmarkResult {
    // Elapsed wall-clock time per iteration (seconds). benchmark() reports the
    // best sample; benchmark_comparison() reports a robust geometric mean.
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
    // For benchmark_comparison(), this is instead the multiplicative half-width
    // of relative_time's approximate 95% confidence interval. It will be <=
    // config.accuracy unless max_time is exceeded.
    double accuracy;

    // For benchmark_comparison(), the estimated time relative to the first
    // operation, plus an approximate paired 95% confidence interval. These are
    // all 1 for benchmark() and for the first operation in a comparison.
    double relative_time{1.0};
    double relative_time_ci95_low{1.0};
    double relative_time_ci95_high{1.0};

    operator double() const {
        return wall_time;
    }
};

inline BenchmarkResult benchmark(const std::function<void()> &op, const BenchmarkConfig &config = {}) {
    BenchmarkResult result{0, 0, 0};

    BenchmarkInternal::warmup(op, config.warmup_time);

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
            times[i] = BenchmarkInternal::time_iterations(op, iters_per_sample);
            result.samples++;
            result.iterations += iters_per_sample;
            total_time += times[i] * iters_per_sample;
        }
        std::sort(times, times + kMinSamples);

        // Any time result <= to this is considered 'zero' here.
        const double kTimeEpsilon = 1e-9;
        if (times[0] < kTimeEpsilon) {
            // If the fastest time is tiny, then trying to use it to predict next_iters
            // can just explode into something unpredictably huge, which could take far too
            // long to complete. Just double iters_per_sample and try again (or terminate if
            // we're over the max).
            iters_per_sample *= 2;
        } else {
            const double time_factor = std::max(times[0] * kMinSamples, kTimeEpsilon);
            if (time_factor * iters_per_sample >= min_time) {
                break;
            }
            // Use an estimate based on initial times to converge faster.
            const double next_iters = std::max(min_time / time_factor,
                                               iters_per_sample * 2.0);
            iters_per_sample = std::lround(next_iters);
        }

        // Ensure we never explode beyond the max.
        if (iters_per_sample >= config.max_iters_per_sample) {
            iters_per_sample = config.max_iters_per_sample;
            break;
        }
    }

    // - Keep taking samples until we are accurate enough (even if we run over min_time).
    // - If we are already accurate enough but have time remaining, keep taking samples.
    // - No matter what, don't go over max_time; this is important, in case
    // we happen to get faster results for the first samples, then happen to transition
    // to throttled-down CPU state.
    while ((times[0] * accuracy < times[kMinSamples - 1] || total_time < min_time) &&
           total_time < max_time) {
        times[kMinSamples] = BenchmarkInternal::time_iterations(op, iters_per_sample);
        result.samples++;
        result.iterations += iters_per_sample;
        total_time += times[kMinSamples] * iters_per_sample;
        std::sort(times, times + kMinSamples + 1);
    }
    result.wall_time = times[0];
    result.accuracy = (times[kMinSamples - 1] / times[0]) - 1.0;

    return result;
}

namespace BenchmarkInternal {

template<typename T, size_t N, size_t... Is>
auto array_to_tuple(const std::array<T, N> &arr, std::index_sequence<Is...>) {
    return std::make_tuple(arr[Is]...);
}

template<typename T, size_t N>
auto array_to_tuple(const std::array<T, N> &arr) {
    return array_to_tuple(arr, std::make_index_sequence<N>{});
}

// Return one row of a balanced Williams design. For even N, N rows balance
// both position and first-order carryover. Odd N requires each row and its
// reverse, for a total of 2N rows.
template<size_t N>
constexpr size_t comparison_design_rows() {
    static_assert(N > 0, "A comparison design requires at least one operation.");
    return N == 1 ? 1 : N * ((N & 1) ? 2 : 1);
}

template<size_t N>
std::array<size_t, N> comparison_order(size_t design_row) {
    static_assert(N > 0, "A comparison order requires at least one operation.");
    std::array<size_t, N> order{};
    if constexpr (N == 1) {
        return order;
    }

    const bool reverse = (N & 1) && (design_row & 1);
    const size_t shift = ((N & 1) ? design_row / 2 : design_row) % N;
    for (size_t position = 0; position < N; position++) {
        const size_t base = position == 0  ? 0 :
                            (position & 1) ? (position + 1) / 2 :
                                             N - position / 2;
        const size_t output_position = reverse ? N - 1 - position : position;
        order[output_position] = (base + shift) % N;
    }
    return order;
}

inline uint64_t comparison_random(uint64_t &state) {
    // SplitMix64 is small, deterministic across standard-library versions,
    // and more than adequate for randomizing experimental-design rows.
    state += 0x9e3779b97f4a7c15ULL;
    uint64_t z = state;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

inline void shuffle_design_rows(std::vector<size_t> &rows, uint64_t seed, uint64_t cycle) {
    std::iota(rows.begin(), rows.end(), 0);
    uint64_t state = seed ^ (cycle * 0xd1b54a32d192ed03ULL);
    for (size_t i = rows.size(); i > 1; i--) {
        const size_t j = comparison_random(state) % i;
        std::swap(rows[i - 1], rows[j]);
    }
}

inline uint64_t calibrate_iterations(const std::function<void()> &op,
                                     double target_time,
                                     uint64_t max_iters_per_sample) {
    const uint64_t max_iters = std::max<uint64_t>(1, max_iters_per_sample);
    uint64_t iterations = 1;
    for (;;) {
        const double time_per_iteration = time_iterations(op, iterations);
        const double elapsed = time_per_iteration * iterations;
        if (elapsed >= target_time * 0.8 || iterations >= max_iters) {
            return iterations;
        }

        long double scale = elapsed > 1e-9 ? target_time / elapsed : 10.0;
        scale = std::max<long double>(2.0, std::min<long double>(scale, 100.0));
        const long double predicted = std::ceil(iterations * scale);
        iterations = predicted >= max_iters ? max_iters : static_cast<uint64_t>(predicted);
    }
}

inline double t95_multiplier(size_t samples) {
    // Two-sided 95% Student-t critical values for 1..30 degrees of freedom.
    static constexpr double critical_values[] = {
        12.706, 4.303, 3.182, 2.776, 2.571, 2.447, 2.365, 2.306,
        2.262, 2.228, 2.201, 2.179, 2.160, 2.145, 2.131, 2.120,
        2.110, 2.101, 2.093, 2.086, 2.080, 2.074, 2.069, 2.064,
        2.060, 2.056, 2.052, 2.048, 2.045, 2.042};
    if (samples < 2) {
        return std::numeric_limits<double>::infinity();
    }
    const size_t degrees_of_freedom = samples - 1;
    if (degrees_of_freedom <= std::size(critical_values)) {
        return critical_values[degrees_of_freedom - 1];
    }
    return 1.96;
}

struct RelativeStatistics {
    double estimate;
    double ci95_low;
    double ci95_high;
    double accuracy;
};

struct TrimmedStatistics {
    double mean;
    double standard_error;
    size_t retained_samples;
};

inline TrimmedStatistics trimmed_statistics(std::vector<double> values) {
    assert(!values.empty());
    std::sort(values.begin(), values.end());
    const size_t trim = values.size() / 10;
    const size_t first = trim;
    const size_t last = values.size() - trim;

    double mean = 0;
    for (size_t i = first; i < last; i++) {
        mean += values[i];
    }
    mean /= last - first;

    if (values.size() < 2) {
        return {mean, std::numeric_limits<double>::infinity(), last - first};
    }

    // Estimate the trimmed mean's standard error from the corresponding
    // Winsorized sample: discarded tails are replaced by their nearest kept
    // value. The scale factor accounts for the reduced central interval.
    double winsorized_mean = 0;
    for (size_t i = 0; i < values.size(); i++) {
        const double winsorized = values[std::min(last - 1, std::max(first, i))];
        winsorized_mean += winsorized;
    }
    winsorized_mean /= values.size();

    double squared_deviations = 0;
    for (size_t i = 0; i < values.size(); i++) {
        const double winsorized = values[std::min(last - 1, std::max(first, i))];
        squared_deviations += (winsorized - winsorized_mean) * (winsorized - winsorized_mean);
    }
    const double winsorized_variance = squared_deviations / (values.size() - 1);
    const double retained_fraction = static_cast<double>(last - first) / values.size();
    const double standard_error = std::sqrt(winsorized_variance / values.size()) / retained_fraction;
    return {mean, standard_error, last - first};
}

inline RelativeStatistics relative_statistics(const std::vector<double> &times,
                                              const std::vector<double> &reference_times) {
    assert(times.size() == reference_times.size());
    if (times.empty()) {
        return {1.0, 0.0, std::numeric_limits<double>::infinity(),
                std::numeric_limits<double>::infinity()};
    }

    std::vector<double> log_ratios;
    log_ratios.reserve(times.size());
    for (size_t i = 0; i < times.size(); i++) {
        const double time = std::max(times[i], std::numeric_limits<double>::min());
        const double reference_time = std::max(reference_times[i], std::numeric_limits<double>::min());
        log_ratios.push_back(std::log(time / reference_time));
    }

    const auto statistics = trimmed_statistics(std::move(log_ratios));
    const double estimate = std::exp(statistics.mean);
    if (times.size() < 2) {
        return {estimate, 0.0, std::numeric_limits<double>::infinity(),
                std::numeric_limits<double>::infinity()};
    }
    const double log_margin = t95_multiplier(statistics.retained_samples) * statistics.standard_error;
    return {estimate,
            std::exp(statistics.mean - log_margin),
            std::exp(statistics.mean + log_margin),
            std::exp(log_margin) - 1.0};
}

inline double robust_geometric_mean(const std::vector<double> &values) {
    assert(!values.empty());
    std::vector<double> log_values;
    log_values.reserve(values.size());
    for (double value : values) {
        value = std::max(value, std::numeric_limits<double>::min());
        log_values.push_back(std::log(value));
    }
    return std::exp(trimmed_statistics(std::move(log_values)).mean);
}

}  // namespace BenchmarkInternal

// Benchmark and compare several operations against each other, returning one
// BenchmarkResult per operation (in argument order) as a tuple. Each operation
// is warmed and calibrated once, then short timed batches are measured in
// counterbalanced, randomized blocks. wall_time is a robust geometric mean of
// the measured batches, rather than the minimum.
//
// Counterbalancing distributes position and predecessor effects equally across
// operations. Short blocks make transient CPU frequency and thermal drift
// common to the observations in a block. The relative fields in each result
// use paired log-ratios against the first operation, so that common drift
// cancels rather than adding noise to the comparison.
//
// min_time and max_time are total measured-time budgets per operation. After
// comparison_rounds and min_time have both been satisfied, measurement stops
// when every paired 95% interval is within config.accuracy, or when max_time is
// reached. Stopping only happens at the end of a complete balanced design.
template<typename... Fns>
auto benchmark_comparison(const BenchmarkConfig &config, Fns &&...fns) {
    constexpr size_t N = sizeof...(Fns);
    static_assert(N > 0, "benchmark_comparison() requires at least one operation to benchmark.");

    std::array<std::function<void()>, N> ops{{std::function<void()>(std::forward<Fns>(fns))...}};

    constexpr size_t design_size = BenchmarkInternal::comparison_design_rows<N>();
    std::vector<size_t> design_rows(design_size);
    uint64_t design_cycle = 0;
    BenchmarkInternal::shuffle_design_rows(design_rows, config.comparison_seed, design_cycle);

    // Warm and calibrate in a balanced row rather than argument order. These
    // calls are discarded; calibration is reused for every measured block.
    const auto setup_order = BenchmarkInternal::comparison_order<N>(design_rows[0]);
    for (size_t i : setup_order) {
        BenchmarkInternal::warmup(ops[i], config.warmup_time);
    }

    const double min_time = std::max(10 * 1e-6, config.min_time);
    const double max_time = std::max(min_time, config.max_time);
    const uint64_t requested_blocks = std::max<uint64_t>(1, config.comparison_rounds);
    const uint64_t min_blocks = ((requested_blocks + design_size - 1) / design_size) * design_size;
    const double configured_sample_time = config.comparison_sample_time > 0 ?
                                              config.comparison_sample_time :
                                              min_time;
    const double sample_time = std::max(10 * 1e-6,
                                        std::min(configured_sample_time,
                                                 min_time / min_blocks));

    std::array<uint64_t, N> iterations_per_sample{};
    for (size_t i : setup_order) {
        iterations_per_sample[i] = BenchmarkInternal::calibrate_iterations(
            ops[i], sample_time, config.max_iters_per_sample);
    }

    std::array<std::vector<double>, N> times;
    std::array<double, N> measured_time{};
    uint64_t blocks = 0;
    for (;;) {
        if (blocks > 0 && blocks % design_size == 0) {
            design_cycle++;
            BenchmarkInternal::shuffle_design_rows(design_rows, config.comparison_seed, design_cycle);
        }
        const size_t row = design_rows[blocks % design_size];
        const auto order = BenchmarkInternal::comparison_order<N>(row);
        for (size_t i : order) {
            const double time = BenchmarkInternal::time_iterations(ops[i], iterations_per_sample[i]);
            times[i].push_back(time);
            measured_time[i] += time * iterations_per_sample[i];
        }
        blocks++;

        if (blocks % design_size != 0) {
            continue;
        }

        bool reached_min_time = true;
        bool reached_max_time = false;
        for (double time : measured_time) {
            reached_min_time &= time >= min_time;
            reached_max_time |= time >= max_time;
        }

        bool accurate = true;
        for (size_t i = 1; i < N; i++) {
            accurate &= BenchmarkInternal::relative_statistics(times[i], times[0]).accuracy <=
                        std::min(0.1, std::max(0.001, config.accuracy));
        }
        if ((blocks >= min_blocks && reached_min_time && accurate) || reached_max_time) {
            break;
        }
    }

    std::array<BenchmarkResult, N> results;
    const double reference_wall_time = BenchmarkInternal::robust_geometric_mean(times[0]);
    for (size_t i = 0; i < N; i++) {
        const auto relative = i == 0 ? BenchmarkInternal::RelativeStatistics{1.0, 1.0, 1.0, 0.0} :
                                       BenchmarkInternal::relative_statistics(times[i], times[0]);
        const uint64_t iterations = iterations_per_sample[i] >
                                            std::numeric_limits<uint64_t>::max() / blocks ?
                                        std::numeric_limits<uint64_t>::max() :
                                        iterations_per_sample[i] * blocks;
        results[i] = BenchmarkResult{reference_wall_time * relative.estimate,
                                     blocks,
                                     iterations,
                                     relative.accuracy,
                                     relative.estimate,
                                     relative.ci95_low,
                                     relative.ci95_high};
    }

    return BenchmarkInternal::array_to_tuple(results);
}

}  // namespace Tools
}  // namespace Halide

#endif
