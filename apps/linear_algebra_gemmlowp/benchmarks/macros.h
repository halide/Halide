#include "../support/benchmark.h"

#define time_it(code)                                        \
    double elapsed = 0;                                      \
    for (int iters = 1; ; iters *= 2) {                      \
        /* Best of 5 */                                      \
        elapsed = 1e6 * benchmark(5, iters, [&]() {code;});  \
        /* spend at least 5x20ms benchmarking */             \
        if (elapsed * iters > 200000) {                      \
            break;                                           \
        }                                                    \
    }

#define L3GFLOPS(N) (3.0 + N) * N * N * 1e-3 / elapsed
#define L3Benchmark(benchmark, type, code)                              \
    virtual void bench_##benchmark(int N) {                             \
        Scalar a_offset = random_scalar();                              \
        Scalar b_offset = random_scalar();                              \
        Scalar c_offset = random_scalar();                              \
        Scalar c_mult_int = random_scalar();                            \
        Scalar c_shift = random_scalar();                               \
        Matrix A(random_matrix(N));                                     \
        Matrix B(random_matrix(N));                                     \
        Matrix C(random_matrix(N));                                     \
                                                                        \
        time_it(code)                                                   \
                                                                        \
        std::cout << std::setw(8) << name                               \
                  << std::setw(15) << type << #benchmark                \
                  << std::setw(8) << std::to_string(N)                  \
                  << std::setw(20) << std::to_string(elapsed)           \
                  << std::setw(20) << L3GFLOPS(N)                       \
                  << std::endl;                                         \
    }
