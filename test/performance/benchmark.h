#ifndef BENCHMARK_H
#define BENCHMARK_H

#include <limits>

// Benchmark the operation 'op'. The number of iterations refers to
// how many times the operation is run for each time measurement, the
// result is the minimum over a number of samples runs. The result is the
// amount of time in seconds for one iteration.
#ifdef _WIN32

extern "C" bool __stdcall QueryPerformanceCounter(uint64_t *);
extern "C" bool __stdcall QueryPerformanceFrequency(uint64_t *);

template <typename F>
double benchmark(int samples, int iterations, F op) {
    uint64_t freq;
    QueryPerformanceFrequency(&freq);

    double best = std::numeric_limits<double>::infinity();
    for (int i = 0; i < samples; i++) {
        uint64_t t1;
        QueryPerformanceCounter(&t1);
        for (int j = 0; j < iterations; j++) {
            op();
        }
        uint64_t t2;
        QueryPerformanceCounter(&t2);
        double dt = (t2 - t1) / static_cast<double>(freq);
        if (dt < best) best = dt;
    }
    return best / iterations;
}

#else

#include <chrono>

template <typename F>
double benchmark(int samples, int iterations, F op) {
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

#endif

#endif
