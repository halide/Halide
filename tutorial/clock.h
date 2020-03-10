#ifndef HALIDE_TUTORIAL_CLOCK_H
#define HALIDE_TUTORIAL_CLOCK_H

// A current_time function for use in the tests.  Returns time in
// milliseconds.

#include "halide_benchmark.h"

inline double current_time() {
    static auto start_time = Halide::Tools::benchmark_now().time_since_epoch();

    auto now = Halide::Tools::benchmark_now().time_since_epoch() - start_time;
    return std::chrono::duration_cast<std::chrono::microseconds>(now).count() / 1e3;
}

#endif  // HALIDE_TUTORIAL_CLOCK_H
