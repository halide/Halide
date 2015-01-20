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

#if defined(USE_ATLAS)
# define BLAS_NAME "Atlas"
#elif defined(USE_OPENBLAS)
# define BLAS_NAME "OpenBLAS"
#else
# define BLAS_NAME "Cblas"
#endif

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
        std::cout << std::setw(8) << name                               \
                  << std::setw(15) << type #benchmark                   \
                  << std::setw(8) << std::to_string(N)                  \
                  << std::setw(20) << std::to_string(elapsed)           \
                  << std::setw(20) << 1000 * N / elapsed                \
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
        std::cout << std::setw(8) << name                               \
                  << std::setw(15) << type #benchmark                   \
                  << std::setw(8) << std::to_string(N)                  \
                  << std::setw(20) << std::to_string(elapsed)           \
                  << std::setw(20) << 1000 * N / elapsed                \
                  << std::endl;                                         \
    }                                                                   \

#define L3Benchmark(benchmark, type, code)                              \
    virtual void bench_##benchmark(int N) {                             \
        Scalar alpha = randomScalar();                                  \
        Scalar beta = randomScalar();                                   \
        std::unique_ptr<Matrix> A(randomMatrix(N));                     \
        std::unique_ptr<Matrix> B(randomMatrix(N));                     \
        std::unique_ptr<Matrix> C(randomMatrix(N));                     \
                                                                        \
        double start = current_time();                                  \
        for (int i = 0; i < num_iters; ++i) {                           \
            code;                                                       \
        }                                                               \
        double end = current_time();                                    \
        double elapsed = end - start;                                   \
                                                                        \
        std::cout << std::setw(8) << name                               \
                  << std::setw(15) << type #benchmark                   \
                  << std::setw(8) << std::to_string(N)                  \
                  << std::setw(20) << std::to_string(elapsed)           \
                  << std::setw(20) << 1000 * N / elapsed                \
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
        } else if (benchmark == "gemv_notrans") {
            this->bench_gemv_notrans(size);
        } else if (benchmark == "gemv_trans") {
            this->bench_gemv_trans(size);
        } else if (benchmark == "gemm_notrans") {
            this->bench_gemm_notrans(size);
        }
    }

    virtual void bench_copy(int N) =0;
    virtual void bench_scal(int N) =0;
    virtual void bench_axpy(int N) =0;
    virtual void bench_dot(int N)  =0;
    virtual void bench_asum(int N) =0;
    virtual void bench_gemv_notrans(int N) =0;
    virtual void bench_gemv_trans(int N) =0;
    virtual void bench_gemm_notrans(int N) =0;
};

struct BenchmarksFloat : public BenchmarksBase<float> {
    BenchmarksFloat(std::string n, int iters) :
            BenchmarksBase(n, iters)
    {}

    Scalar result;

    L1Benchmark(copy, "s", cblas_scopy(N, &(x->front()), 1, &(y->front()), 1))
    L1Benchmark(scal, "s", cblas_sscal(N, alpha, &(x->front()), 1))
    L1Benchmark(axpy, "s", cblas_saxpy(N, alpha, &(x->front()), 1, &(y->front()), 1))
    L1Benchmark(dot,  "s", result = cblas_sdot(N, &(x->front()), 1, &(y->front()), 1))
    L1Benchmark(asum, "s", result = cblas_sasum(N, &(x->front()), 1))

    L2Benchmark(gemv_notrans, "s", cblas_sgemv(CblasColMajor, CblasNoTrans, N, N,
                                               alpha, &(A->front()), N, &(x->front()), 1,
                                               beta, &(y->front()), 1))

    L2Benchmark(gemv_trans, "s", cblas_sgemv(CblasColMajor, CblasTrans, N, N,
                                             alpha, &(A->front()), N, &(x->front()), 1,
                                             beta, &(y->front()), 1))

    L3Benchmark(gemm_notrans, "s", cblas_sgemm(CblasColMajor, CblasNoTrans, CblasNoTrans, N, N, N,
                                               alpha, &(A->front()), N, &(B->front()), N,
                                               beta, &(C->front()), N))

    L3Benchmark(gemm_transA, "s", cblas_sgemm(CblasColMajor, CblasTrans, CblasNoTrans, N, N, N,
                                              alpha, &(A->front()), N, &(B->front()), N,
                                              beta, &(C->front()), N))

    L3Benchmark(gemm_transB, "s", cblas_sgemm(CblasColMajor, CblasNoTrans, CblasTrans, N, N, N,
                                              alpha, &(A->front()), N, &(B->front()), N,
                                              beta, &(C->front()), N))

    L3Benchmark(gemm_transAB, "s", cblas_sgemm(CblasColMajor, CblasTrans, CblasTrans, N, N, N,
                                               alpha, &(A->front()), N, &(B->front()), N,
                                               beta, &(C->front()), N))
};

struct BenchmarksDouble : public BenchmarksBase<double> {
    BenchmarksDouble(std::string n, int iters) :
            BenchmarksBase(n, iters)
    {}

    Scalar result;

    L1Benchmark(copy, "d", cblas_dcopy(N, &(x->front()), 1, &(y->front()), 1))
    L1Benchmark(scal, "d", cblas_dscal(N, alpha, &(x->front()), 1))
    L1Benchmark(axpy, "d", cblas_daxpy(N, alpha, &(x->front()), 1, &(y->front()), 1))
    L1Benchmark(dot,  "d", result = cblas_ddot(N, &(x->front()), 1, &(y->front()), 1))
    L1Benchmark(asum, "d", result = cblas_dasum(N, &(x->front()), 1))

    L2Benchmark(gemv_notrans, "d", cblas_dgemv(CblasColMajor, CblasNoTrans, N, N,
                                                    alpha, &(A->front()), N, &(x->front()), 1,
                                                    beta, &(y->front()), 1))

    L2Benchmark(gemv_trans, "d", cblas_dgemv(CblasColMajor, CblasTrans, N, N,
                                                  alpha, &(A->front()), N, &(x->front()), 1,
                                                  beta, &(y->front()), 1))

    L3Benchmark(gemm_notrans, "d", cblas_dgemm(CblasColMajor, CblasNoTrans, CblasNoTrans, N, N, N,
                                               alpha, &(A->front()), N, &(B->front()), N,
                                               beta, &(C->front()), N))

    L3Benchmark(gemm_transA, "d", cblas_dgemm(CblasColMajor, CblasTrans, CblasNoTrans, N, N, N,
                                              alpha, &(A->front()), N, &(B->front()), N,
                                              beta, &(C->front()), N))

    L3Benchmark(gemm_transB, "d", cblas_dgemm(CblasColMajor, CblasNoTrans, CblasTrans, N, N, N,
                                              alpha, &(A->front()), N, &(B->front()), N,
                                              beta, &(C->front()), N))

    L3Benchmark(gemm_transAB, "d", cblas_dgemm(CblasColMajor, CblasTrans, CblasTrans, N, N, N,
                                               alpha, &(A->front()), N, &(B->front()), N,
                                               beta, &(C->front()), N))
};

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cout << "USAGE: cblas_benchmarks <subroutine> <size>\n";
        return 0;
    }

    std::string subroutine = argv[1];
    char type = subroutine[0];
    int  size = std::stoi(argv[2]);

    subroutine = subroutine.substr(1);
    if (type == 's') {
        BenchmarksFloat (BLAS_NAME, 1000).run(subroutine, size);
    } else if (type == 'd') {
        BenchmarksDouble(BLAS_NAME, 1000).run(subroutine, size);
    }

    return 0;
}
