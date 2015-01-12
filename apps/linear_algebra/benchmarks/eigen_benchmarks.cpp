// USAGE: eigen_benchmarks <subroutine> <size>
//
// Benchmarks BLAS subroutines using Eigen's implementation. Will
// construct random size x size matrices and/or size x 1 vectors
// to test the subroutine with.
//
// Accepted values for subroutine are:
//    L1: scal, copy, axpy, dot, nrm2
//    L2: gemv
//    L3: gemm
//

#include <iomanip>
#include <iostream>
#include <string>
#include <Eigen/Eigen>
#include "clock.h"

#define L1Benchmark(benchmark, code)                                    \
    void bench_##benchmark(int N) {                                     \
        Scalar alpha = randomScalar();                                  \
        Vector x = randomVector(N);                                     \
        Vector y = randomVector(N);                                     \
                                                                        \
        double start = current_time();                                  \
        for (int i = 0; i < num_iters; ++i) {                           \
            code;                                                       \
        }                                                               \
        double end = current_time();                                    \
        double elapsed = end - start;                                   \
                                                                        \
        std::cout << std::setw(15) << name + "::" + #benchmark          \
                  << std::setw(8) << std::to_string(N)                  \
                  << std::setw(20) << std::to_string(elapsed) + "(ms)"  \
                  << std::setw(25) << items_per_second(N, elapsed)      \
                  << std::endl;                                         \
    }                                                                   \

#define L2Benchmark(benchmark, code)                                    \
    void bench_##benchmark(int N) {                                     \
        Scalar alpha = randomScalar();                                  \
        Scalar beta = randomScalar();                                   \
        Vector x = randomVector(N);                                     \
        Vector y = randomVector(N);                                     \
        Matrix A = randomMatrix(N);                                     \
                                                                        \
        double start = current_time();                                  \
        for (int i = 0; i < num_iters; ++i) {                           \
            code;                                                       \
        }                                                               \
        double end = current_time();                                    \
        double elapsed = end - start;                                   \
                                                                        \
        std::cout << std::setw(15) << name + "::" + #benchmark          \
                  << std::setw(8) << std::to_string(N)                  \
                  << std::setw(20) << std::to_string(elapsed) + "(ms)"  \
                  << std::setw(25) << items_per_second(N, elapsed)      \
                  << std::endl;                                         \
    }                                                                   \

template<class T>
struct Benchmarks {
    typedef T Scalar;
    typedef Eigen::Matrix<T, Eigen::Dynamic, 1> Vector;
    typedef Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic> Matrix;

    Scalar randomScalar() {
        Vector x(1);
        x.setRandom();
        return x[0];
    }

    Vector randomVector(int N) {
        Vector x(N);
        x.setRandom();
        return x;
    }

    Matrix randomMatrix(int N) {
        Matrix A(N, N);
        A.setRandom();
        return A;
    }

    Benchmarks(std::string n, int iters) :
            name(n), num_iters(iters)
    {}

    void run(std::string benchmark, int size) {
        if (benchmark == "copy") {
            bench_copy(size);
        } else if (benchmark == "scal") {
            bench_scal(size);
        } else if (benchmark == "axpy") {
            bench_axpy(size);
        } else if (benchmark == "dot") {
            bench_dot(size);
        } else if (benchmark == "asum") {
            bench_asum(size);
        } else if (benchmark == "gemv") {
            bench_gemv(size);
        }
    }

    Scalar result;

    L1Benchmark(copy, y = x);
    L1Benchmark(scal, x = alpha * x);
    L1Benchmark(axpy, y = alpha * x + y);
    L1Benchmark(dot,  result = x.dot(y));
    L1Benchmark(asum, result = x.array().abs().sum());

    L2Benchmark(gemv, y = alpha * A * x + beta * y);

  private:
    std::string name;
    int num_iters;
};

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cout << "USAGE: eigen_benchmarks <subroutine> <size>\n";
        return 0;
    }

    Benchmarks<float> ("Eigen", 1000).run(argv[1], std::stoi(argv[2]));
    Benchmarks<double>("Eigen", 1000).run(argv[1], std::stoi(argv[2]));

    return 0;
}
