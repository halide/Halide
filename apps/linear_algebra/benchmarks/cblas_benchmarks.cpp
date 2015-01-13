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
#include <cblas.h>
#include "Halide.h"
#include "clock.h"

#define L1Benchmark(benchmark, type, code)                              \
    virtual void bench_##benchmark(int N) {                             \
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
    typedef std::vector<T> Vector;
    typedef std::vector<T> Matrix;

    std::random_device rand_dev;
    std::default_random_engine rand_eng{rand_dev()};

    std::string name;
    int num_iters;

    Scalar randomScalar() {
        std::uniform_real_distribution<T> uniform_dist(0.0, 1.0);
        return uniform_dist(rand_eng);
    }

    Vector *randomVector(int N) {
        Vector *buff = new Vector(N);
        Vector &x = *buff;
        for (int i=0; i<N; ++i) {
            x[i] = randomScalar();
        }
        return buff;
    }

    Matrix *randomMatrix(int N) {
        Matrix *buff = new Matrix(N * N);
        Matrix &A = *buff;
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
            BenchmarksBase(n, iters)
    {}

    Scalar result;

    L1Benchmark(copy, "float", cblas_scopy(N, &(x->front()), 1, &(y->front()), 1))
    L1Benchmark(scal, "float", cblas_sscal(N, alpha, &(x->front()), 1))
    L1Benchmark(axpy, "float", cblas_saxpy(N, alpha, &(x->front()), 1, &(y->front()), 1))
    L1Benchmark(dot,  "float", result = cblas_sdot(N, &(x->front()), 1, &(y->front()), 1))
    L1Benchmark(asum, "float", result = cblas_sasum(N, &(x->front()), 1))

    // L2Benchmark(gemv, "float", cblas_sgemv(CblasColMajor, CblasNoTrans, N, N,
    //                                        alpha, &(A->front()), N, &(x->front()), 1,
    //                                        beta, &(y->front()), 1))

    L2Benchmark(gemv, "float", cblas_sgemv(CblasColMajor, CblasTrans, N, N,
                                           alpha, &(A->front()), N, &(x->front()), 1,
                                           beta, &(y->front()), 1))

};

struct BenchmarksDouble : public BenchmarksBase<double> {
    BenchmarksDouble(std::string n, int iters) :
            BenchmarksBase(n, iters)
    {}

    Scalar result;

    L1Benchmark(copy, "double", cblas_dcopy(N, &(x->front()), 1, &(y->front()), 1))
    L1Benchmark(scal, "double", cblas_dscal(N, alpha, &(x->front()), 1))
    L1Benchmark(axpy, "double", cblas_daxpy(N, alpha, &(x->front()), 1, &(y->front()), 1))
    L1Benchmark(dot,  "double", result = cblas_ddot(N, &(x->front()), 1, &(y->front()), 1))
    L1Benchmark(asum, "double", result = cblas_dasum(N, &(x->front()), 1))

    // L2Benchmark(gemv, "double", cblas_dgemv(CblasColMajor, CblasNoTrans, N, N,
    //                                         alpha, &(A->front()), N, &(x->front()), 1,
    //                                         beta, &(y->front()), 1))

    L2Benchmark(gemv, "double", cblas_dgemv(CblasColMajor, CblasTrans, N, N,
                                            alpha, &(A->front()), N, &(x->front()), 1,
                                            beta, &(y->front()), 1))
};

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cout << "USAGE: cblas_benchmarks <subroutine> <size>\n";
        return 0;
    }

    BenchmarksFloat ("Cblas", 1000).run(argv[1], std::stoi(argv[2]));
    BenchmarksDouble("Cblas", 1000).run(argv[1], std::stoi(argv[2]));

    return 0;
}
