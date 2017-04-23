#include "halide_benchmark.h"

#define time_it(code)                                        \
    double elapsed = 0;                                      \
    for (int iters = 1; ; iters *= 2) {                      \
        /* Best of 5 */                                      \
        elapsed = 1e6 * Halide::Tools::benchmark(5, iters, [&]() {code;});  \
        /* spend at least 5x20ms benchmarking */             \
        if (elapsed * iters > 20000) {                       \
            break;                                           \
        }                                                    \
    }

#define L1GFLOPS(N) 2.0 * N * 1e-3 / elapsed
#define L1Benchmark(benchmark, type, code)                              \
    virtual void bench_##benchmark(int N) {                             \
        Scalar alpha = random_scalar();                                 \
        (void) alpha;                                                   \
        Vector x(random_vector(N));                                     \
        Vector y(random_vector(N));                                     \
                                                                        \
        time_it(code)                                                   \
                                                                        \
        std::cout << std::setw(8) << name                               \
                  << std::setw(15) << type << #benchmark                \
                  << std::setw(8) << std::to_string(N)                  \
                  << std::setw(20) << std::to_string(elapsed)           \
                  << std::setw(20) << L1GFLOPS(N)                       \
                  << std::endl;                                         \
    }

#define L2GFLOPS(N) (2.0 + N) * N * 1e-3 / elapsed
#define L2Benchmark(benchmark, type, code)                              \
    virtual void bench_##benchmark(int N) {                             \
        Scalar alpha = random_scalar();                                 \
        Scalar beta = random_scalar();                                  \
        (void) alpha;                                                   \
        (void) beta;                                                    \
        Vector x(random_vector(N));                                     \
        Vector y(random_vector(N));                                     \
        Matrix A(random_matrix(N));                                     \
                                                                        \
        time_it(code)                                                   \
                                                                        \
        std::cout << std::setw(8) << name                               \
        << std::setw(15) << type << #benchmark                          \
                  << std::setw(8) << std::to_string(N)                  \
                  << std::setw(20) << std::to_string(elapsed)           \
                  << std::setw(20) << L2GFLOPS(N)                       \
                  << std::endl;                                         \
    }

#define L3GFLOPS(N) (3.0 + N) * N * N * 1e-3 / elapsed
#define L3Benchmark(benchmark, type, code)                              \
    virtual void bench_##benchmark(int N) {                             \
        Scalar alpha = random_scalar();                                 \
        Scalar beta = random_scalar();                                  \
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
