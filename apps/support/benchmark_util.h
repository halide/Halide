#include <iostream>
#include <functional>

#include "halide_benchmark.h"

inline void three_way_bench(std::function<void()> manual,
                            std::function<void()> auto_classic,
                            std::function<void()> auto_new,
                            std::ostream& output = std::cout) {

    Halide::Tools::BenchmarkConfig config;
    config.accuracy = 0.005;

    double t;

    if (manual) {
        manual();
        t = Halide::Tools::benchmark(manual, config);
        output << "Manually-tuned time: " << t * 1e3 << " ms\n";
    }

    if (auto_classic) {
        auto_classic();
        t = Halide::Tools::benchmark(auto_classic, config);
        output << "Classic auto-scheduled time: " << t * 1e3 << " ms\n";
    }

    if (auto_new) {
        auto_new();
        t = Halide::Tools::benchmark(auto_new, config);
        output << "Auto-scheduled : " << t * 1e3 << " ms\n";
    }
}
