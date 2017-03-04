#ifndef BENCHMARK_H
#define BENCHMARK_H

// Benchmark the operation 'op'. The number of iterations refers to
// how many times the operation is run for each time measurement, the
// result is the minimum over a number of samples runs. The result is the
// amount of time in seconds for one iteration.
#ifdef _WIN32

union _LARGE_INTEGER;
typedef union _LARGE_INTEGER LARGE_INTEGER;
extern "C" int __stdcall QueryPerformanceCounter(LARGE_INTEGER*);
extern "C" int __stdcall QueryPerformanceFrequency(LARGE_INTEGER*);

template <typename F>
double benchmark(int samples, int iterations, F op) {
    int64_t freq;
    QueryPerformanceFrequency((LARGE_INTEGER*)&freq);

    double best = 1e100;
    for (int i = 0; i < samples; i++) {
        int64_t t1;
        QueryPerformanceCounter((LARGE_INTEGER*)&t1);
        for (int j = 0; j < iterations; j++) {
            op();
        }
        int64_t t2;
        QueryPerformanceCounter((LARGE_INTEGER*)&t2);
        double dt = (t2 - t1) / static_cast<double>(freq);
        if (dt < best) best = dt;
    }
    return best / iterations;
}

#else

#include <sys/time.h>

template <typename F>
double benchmark(int samples, int iterations, F op) {
    double best = 1e100;
    for (int i = 0; i < samples; i++) {
        timeval t1;
        gettimeofday(&t1, NULL);
        for (int j = 0; j < iterations; j++) {
            op();
        }
        timeval t2;
        gettimeofday(&t2, NULL);

        double dt = (t2.tv_usec - t1.tv_usec)*1e-6 + (t2.tv_sec - t1.tv_sec);
        if (dt < best) best = dt;
    }
    return best / iterations;
}

#endif

#endif
