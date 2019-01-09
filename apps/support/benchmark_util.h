#include <iostream>
#include <functional>

#include "halide_benchmark.h"

inline void multi_way_bench(const std::vector<std::pair<std::string, std::function<void()>>> &funcs,
                            uint64_t samples = 10,
                            uint64_t iterations = 10,
                            std::ostream& output = std::cout) {

    double t;
    for (auto name_func : funcs) {
        auto name = name_func.first;
        auto func = name_func.second;
        func();
        t = Halide::Tools::benchmark(samples, iterations, func);
        output << name << " time: " << t * 1e3 << "ms\n";
    }
}

inline void three_way_bench(std::function<void()> manual,
                            std::function<void()> auto_classic,
                            std::function<void()> auto_new,
                            uint64_t samples = 10,
                            uint64_t iterations = 10,
                            std::ostream& output = std::cout) {

    double t;

    if (manual) {
        manual();
        t = Halide::Tools::benchmark(samples, iterations, manual);
        output << "Manually-tuned time: " << t * 1e3 << "ms\n";
    }

    if (auto_classic) {
        auto_classic();
        t = Halide::Tools::benchmark(samples, iterations, auto_classic);
        output << "Classic auto-scheduled time: " << t * 1e3 << "ms\n";
    }

    if (auto_new) {
        auto_new();
        t = Halide::Tools::benchmark(samples, iterations, auto_new);
        output << "Auto-scheduled : " << t * 1e3 << "ms\n";
    }
}
