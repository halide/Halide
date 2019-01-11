#include <iostream>
#include <functional>

#include "halide_benchmark.h"

inline void three_way_bench(std::function<void()> manual,
                            std::function<void()> auto_classic,
                            std::function<void()> auto_new,
                            std::ostream& output = std::cout) {

    const auto is_one = [](const char *key) -> bool {
        const char *value = getenv(key);
        return value && value[0] == '1' && value[1] == 0;
    };

    Halide::Tools::BenchmarkConfig config;
    config.accuracy = 0.005;

    double t;

    if (manual && !is_one("HL_THREE_WAY_BENCH_SKIP_MANUAL")) {
        manual();
        t = Halide::Tools::benchmark(manual, config);
        output << "Manually-tuned time: " << t * 1e3 << " ms\n";
    }

    if (auto_classic && !is_one("HL_THREE_WAY_BENCH_SKIP_AUTO_CLASSIC")) {
        auto_classic();
        t = Halide::Tools::benchmark(auto_classic, config);
        output << "Classic auto-scheduled time: " << t * 1e3 << " ms\n";
    }

    if (auto_new && !is_one("HL_THREE_WAY_BENCH_SKIP_AUTO_NEW")) {
        auto_new();
        t = Halide::Tools::benchmark(auto_new, config);
        output << "Auto-scheduled : " << t * 1e3 << " ms\n";
    }
}
