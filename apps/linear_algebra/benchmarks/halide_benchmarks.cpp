// USAGE: halide_benchmarks <subroutine> <size>
//
// Benchmarks BLAS subroutines using Halide's implementation. Will
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
#include <random>
#include <string>
#include "Halide.h"
#include "halide_blas.h"
#include "clock.h"

#define L1Benchmark(benchmark, type, code)                               \
  virtual void bench_##benchmark(int N) {                               \
        Scalar alpha = randomScalar();                                  \
        std::unique_ptr<Vector> x(randomVector(N));                     \
        std::unique_ptr<Vector> y(randomVector(N));                     \
                                                                        \
        double start = current_time();                                  \
        for (int i = 0; i < num_iters; ++i) {                           \
            code;                                                       \
        }                                                               \
        double end = current_time();                                    \
        double elapsed = end - start;                                   \
                                                                        \
        std::cout << std::setw(15) << name + "::" + #benchmark          \
                  << std::setw(8) << type                               \
                  << std::setw(8) << std::to_string(N)                  \
                  << std::setw(20) << std::to_string(elapsed) + "(ms)"  \
                  << std::setw(20) << items_per_second(N, elapsed)      \
                  << std::endl;                                         \
    }                                                                   \

#define L2Benchmark(benchmark, type, code)                              \
    virtual void bench_##benchmark(int N) {                             \
        Scalar alpha = randomScalar();                                  \
        Scalar beta = randomScalar();                                   \
        std::unique_ptr<Vector> x(randomVector(N));                     \
        std::unique_ptr<Vector> y(randomVector(N));                     \
        std::unique_ptr<Matrix> A(randomMatrix(N));                     \
                                                                        \
        double start = current_time();                                  \
        for (int i = 0; i < num_iters; ++i) {                           \
            code;                                                       \
        }                                                               \
        double end = current_time();                                    \
        double elapsed = end - start;                                   \
                                                                        \
        std::cout << std::setw(15) << name + "::" + #benchmark          \
                  << std::setw(8) << type                               \
                  << std::setw(8) << std::to_string(N)                  \
                  << std::setw(20) << std::to_string(elapsed) + "(ms)"  \
                  << std::setw(20) << items_per_second(N, elapsed)      \
                  << std::endl;                                         \
    }                                                                   \


template<class T>
struct BenchmarksBase {
    typedef T Scalar;
    typedef Halide::Buffer Vector;
    typedef Halide::Buffer Matrix;

    std::random_device rand_dev;
    std::default_random_engine rand_eng{rand_dev()};

    std::string name;
    int num_iters;

    Scalar randomScalar() {
        std::uniform_real_distribution<T> uniform_dist(0.0, 1.0);
        return uniform_dist(rand_eng);
    }

    Vector *randomVector(int N) {
        Vector *buff = new Vector(Halide::type_of<T>(), N);
        Scalar *x = (Scalar*)buff->host_ptr();
        for (int i=0; i<N; ++i) {
            x[i] = randomScalar();
        }
        return buff;
    }

    Matrix *randomMatrix(int N) {
        Matrix *buff = new Matrix(Halide::type_of<T>(), N, N);
        Scalar *A = (Scalar*)buff->host_ptr();
        for (int i=0; i<N*N; ++i) {
            A[i] = randomScalar();
        }
        return buff;
    }

    BenchmarksBase(std::string n, int iters) :
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
            this->bench_gemv(size);
        }
    }

    virtual void bench_copy(int N) =0;
    virtual void bench_scal(int N) =0;
    virtual void bench_axpy(int N) =0;
    virtual void bench_dot(int N)  =0;
    virtual void bench_asum(int N) =0;
    virtual void bench_gemv(int N) =0;
};

struct BenchmarksFloat : public BenchmarksBase<float> {
    BenchmarksFloat(std::string n, int iters) :
            BenchmarksBase(n, iters),
            result(Halide::Float(32), 1)
    {}

    Halide::Buffer result;

    L1Benchmark(copy, "float", halide_scopy(x->raw_buffer(), y->raw_buffer()))
    L1Benchmark(scal, "float", halide_sscal(alpha, x->raw_buffer()))
    L1Benchmark(axpy, "float", halide_saxpy(alpha, x->raw_buffer(), y->raw_buffer()))
    L1Benchmark(dot,  "float", halide_sdot(x->raw_buffer(), y->raw_buffer(), result.raw_buffer()))
    L1Benchmark(asum, "float", halide_sasum(x->raw_buffer(), result.raw_buffer()))

    // L2Benchmark(gemv, "float", halide_sgemv(false, alpha, A->raw_buffer(), x->raw_buffer(),
    //                                beta, y->raw_buffer()))

    L2Benchmark(gemv, "float", halide_sgemv(true, alpha, A->raw_buffer(), x->raw_buffer(),
                                            beta, y->raw_buffer()))
};

struct BenchmarksDouble : public BenchmarksBase<double> {
    BenchmarksDouble(std::string n, int iters) :
            BenchmarksBase(n, iters),
            result(Halide::Float(64), 1)
    {}

    Halide::Buffer result;

    L1Benchmark(copy, "double", halide_dcopy(x->raw_buffer(), y->raw_buffer()))
    L1Benchmark(scal, "double", halide_dscal(alpha, x->raw_buffer()))
    L1Benchmark(axpy, "double", halide_daxpy(alpha, x->raw_buffer(), y->raw_buffer()))
    L1Benchmark(dot,  "double", halide_ddot(x->raw_buffer(), y->raw_buffer(), result.raw_buffer()))
    L1Benchmark(asum, "double", halide_dasum(x->raw_buffer(), result.raw_buffer()))

    // L2Benchmark(gemv, "double", halide_dgemv(false, alpha, A->raw_buffer(), x->raw_buffer(),
    //                                          beta, y->raw_buffer()))

    L2Benchmark(gemv, "double", halide_dgemv(true, alpha, A->raw_buffer(), x->raw_buffer(),
                                             beta, y->raw_buffer()))
};

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cout << "USAGE: halide_benchmarks <subroutine> <size>\n";
        return 0;
    }

    BenchmarksFloat ("Halide", 1000).run(argv[1], std::stoi(argv[2]));
    BenchmarksDouble("Halide", 1000).run(argv[1], std::stoi(argv[2]));

    return 0;
}
