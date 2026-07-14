#include "halide_benchmark.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace Halide::Tools;

namespace {

template<size_t N>
bool check_comparison_design() {
    constexpr size_t rows = BenchmarkInternal::comparison_design_rows<N>();
    std::array<std::array<size_t, N>, N> positions{};
    std::array<std::array<size_t, N>, N> predecessors{};

    for (size_t row = 0; row < rows; row++) {
        const auto order = BenchmarkInternal::comparison_order<N>(row);
        std::array<bool, N> seen{};
        for (size_t position = 0; position < N; position++) {
            const size_t operation = order[position];
            if (operation >= N || seen[operation]) {
                return false;
            }
            seen[operation] = true;
            positions[operation][position]++;
            if (position > 0) {
                predecessors[order[position - 1]][operation]++;
            }
        }
    }

    const size_t expected = rows / N;
    for (size_t i = 0; i < N; i++) {
        for (size_t j = 0; j < N; j++) {
            if (positions[i][j] != expected) {
                return false;
            }
            if (N > 1 && i != j && predecessors[i][j] != expected) {
                return false;
            }
        }
    }
    return true;
}

bool check_shuffle() {
    std::vector<size_t> a(10), b(10);
    BenchmarkInternal::shuffle_design_rows(a, 1234, 7);
    BenchmarkInternal::shuffle_design_rows(b, 1234, 7);
    if (a != b) {
        return false;
    }

    std::array<bool, 10> seen{};
    for (size_t row : a) {
        if (row >= seen.size() || seen[row]) {
            return false;
        }
        seen[row] = true;
    }
    return true;
}

bool check_statistics() {
    const std::vector<double> reference{1, 2, 4, 8};
    const std::vector<double> twice_reference{2, 4, 8, 16};
    const auto statistics = BenchmarkInternal::relative_statistics(twice_reference, reference);
    const std::vector<double> stable_reference(20, 1.0);
    std::vector<double> with_outlier(20, 2.0);
    with_outlier.back() = 2e9;
    const auto robust_statistics = BenchmarkInternal::relative_statistics(with_outlier, stable_reference);
    constexpr double epsilon = 1e-12;
    return std::abs(statistics.estimate - 2.0) < epsilon &&
           std::abs(statistics.ci95_low - 2.0) < epsilon &&
           std::abs(statistics.ci95_high - 2.0) < epsilon &&
           statistics.accuracy < epsilon &&
           std::abs(robust_statistics.estimate - 2.0) < epsilon &&
           robust_statistics.accuracy < epsilon &&
           std::abs(BenchmarkInternal::robust_geometric_mean(reference) - std::sqrt(8.0)) < epsilon;
}

bool check_benchmark_smoke_test() {
    volatile uint64_t value = 0;
    BenchmarkConfig config;
    config.warmup_time = 0;
    config.min_time = 0.002;
    config.max_time = 0.01;
    config.accuracy = 0.1;
    config.comparison_rounds = 4;
    config.comparison_sample_time = 0.00025;

    auto op = [&]() {
        value++;
    };
    const auto [first, second] = benchmark_comparison(config, op, op);
    const double ratio_from_wall_times = second.wall_time / first.wall_time;
    return first.samples >= config.comparison_rounds &&
           first.samples == second.samples &&
           first.samples % BenchmarkInternal::comparison_design_rows<2>() == 0 &&
           std::abs(ratio_from_wall_times - second.relative_time) < 1e-12 &&
           second.relative_time_ci95_low <= second.relative_time &&
           second.relative_time <= second.relative_time_ci95_high;
}

}  // namespace

int main(int argc, char **argv) {
    if (!check_comparison_design<1>() ||
        !check_comparison_design<2>() ||
        !check_comparison_design<3>() ||
        !check_comparison_design<4>() ||
        !check_comparison_design<5>() ||
        !check_comparison_design<6>() ||
        !check_shuffle() ||
        !check_statistics() ||
        !check_benchmark_smoke_test()) {
        printf("Failure!\n");
        return 1;
    }

    printf("Success!\n");
    return 0;
}
