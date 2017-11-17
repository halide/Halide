// USAGE: halide_benchmarks <subroutine> <size>
//
// Benchmarks BLAS subroutines using Halide's implementation. Will
// construct random size x size matrices and/or size x 1 vectors
// to test the subroutine with.
//
// Accepted values for subroutine are:
//    L1: scal, copy, axpy, dot, nrm2
//    L2: gemv_notrans, gemv_trans
//    L3: gemm_notrans, gemm_trans_A, gemm_trans_B, gemm_trans_AB
//

#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include "clock.h"

#if defined(USE_HALIDE)
# define BLAS_NAME "halide"
# include "Halide.h"
#elif defined(USE_ATLAS)
# define BLAS_NAME "Atlas"
extern "C" {
# include <cblas.h>
}
#elif defined(USE_OPENBLAS)
# define BLAS_NAME "OpenBLAS"
# include <cblas.h>
#elif defined(USE_CBLAS)
# define BLAS_NAME "CBlas"
extern "C" {
# include <cblas.h>
}
#else
# error "unknown blas"
#endif

#include "macros.h"

template<class T>
struct BenchmarksBase {
    typedef T Scalar;
    typedef std::vector<T> Vector;
    typedef std::vector<T> Matrix;

    std::random_device rand_dev;
    std::default_random_engine rand_eng{rand_dev()};

    std::string name;

    Scalar random_scalar() {
        std::uniform_real_distribution<T> uniform_dist(0.0, 1.0);
        return uniform_dist(rand_eng);
    }

    Vector random_vector(int N) {
        Vector buff(N);
        for (int i=0; i<N; ++i) {
            buff[i] = random_scalar();
        }
        return buff;
    }

    Matrix random_matrix(int N) {
        Matrix buff(N * N);
        for (int i=0; i<N*N; ++i) {
            buff[i] = random_scalar();
        }
        return buff;
    }

    BenchmarksBase(std::string n) : name(n) {}

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
        } else if (benchmark == "ger") {
            this->bench_ger(size);
        } else if (benchmark == "gemm_notrans") {
            this->bench_gemm_notrans(size);
        } else if (benchmark == "gemm_transA") {
            this->bench_gemm_transA(size);
        } else if (benchmark == "gemm_transB") {
            this->bench_gemm_transB(size);
        } else if (benchmark == "gemm_transAB") {
            this->bench_gemm_transAB(size);
        }
    }

    virtual void bench_copy(int N) =0;
    virtual void bench_scal(int N) =0;
    virtual void bench_axpy(int N) =0;
    virtual void bench_dot(int N)  =0;
    virtual void bench_asum(int N) =0;
    virtual void bench_gemv_notrans(int N) =0;
    virtual void bench_gemv_trans(int N) =0;
    virtual void bench_ger(int N) =0;
    virtual void bench_gemm_notrans(int N) =0;
    virtual void bench_gemm_transA(int N) =0;
    virtual void bench_gemm_transB(int N) =0;
    virtual void bench_gemm_transAB(int N) =0;
};

struct BenchmarksFloat : public BenchmarksBase<float> {
    BenchmarksFloat(std::string n) :
            BenchmarksBase(n)
    {}

    Scalar result;

    L1Benchmark(copy, "s", cblas_scopy(N, &(x[0]), 1, &(y[0]), 1))
    L1Benchmark(scal, "s", cblas_sscal(N, alpha, &(x[0]), 1))
    L1Benchmark(axpy, "s", cblas_saxpy(N, alpha, &(x[0]), 1, &(y[0]), 1))
    L1Benchmark(dot,  "s", result = cblas_sdot(N, &(x[0]), 1, &(y[0]), 1))
    L1Benchmark(asum, "s", result = cblas_sasum(N, &(x[0]), 1))

    L2Benchmark(gemv_notrans, "s", cblas_sgemv(CblasColMajor, CblasNoTrans, N, N,
                                               alpha, &(A[0]), N, &(x[0]), 1,
                                               beta, &(y[0]), 1))

    L2Benchmark(gemv_trans, "s", cblas_sgemv(CblasColMajor, CblasTrans, N, N,
                                             alpha, &(A[0]), N, &(x[0]), 1,
                                             beta, &(y[0]), 1))

    L2Benchmark(ger, "s", cblas_sger(CblasColMajor, N, N, alpha, &(x[0]), 1,
                                     &(y[0]), 1, &(A[0]), N))

    L3Benchmark(gemm_notrans, "s", cblas_sgemm(CblasColMajor, CblasNoTrans, CblasNoTrans, N, N, N,
                                               alpha, &(A[0]), N, &(B[0]), N,
                                               beta, &(C[0]), N))

    L3Benchmark(gemm_transA, "s", cblas_sgemm(CblasColMajor, CblasTrans, CblasNoTrans, N, N, N,
                                              alpha, &(A[0]), N, &(B[0]), N,
                                              beta, &(C[0]), N))

    L3Benchmark(gemm_transB, "s", cblas_sgemm(CblasColMajor, CblasNoTrans, CblasTrans, N, N, N,
                                              alpha, &(A[0]), N, &(B[0]), N,
                                              beta, &(C[0]), N))

    L3Benchmark(gemm_transAB, "s", cblas_sgemm(CblasColMajor, CblasTrans, CblasTrans, N, N, N,
                                               alpha, &(A[0]), N, &(B[0]), N,
                                               beta, &(C[0]), N))
};

struct BenchmarksDouble : public BenchmarksBase<double> {
    BenchmarksDouble(std::string n) :
            BenchmarksBase(n)
    {}

    Scalar result;

    L1Benchmark(copy, "d", cblas_dcopy(N, &(x[0]), 1, &(y[0]), 1))
    L1Benchmark(scal, "d", cblas_dscal(N, alpha, &(x[0]), 1))
    L1Benchmark(axpy, "d", cblas_daxpy(N, alpha, &(x[0]), 1, &(y[0]), 1))
    L1Benchmark(dot,  "d", result = cblas_ddot(N, &(x[0]), 1, &(y[0]), 1))
    L1Benchmark(asum, "d", result = cblas_dasum(N, &(x[0]), 1))

    L2Benchmark(gemv_notrans, "d", cblas_dgemv(CblasColMajor, CblasNoTrans, N, N,
                                                    alpha, &(A[0]), N, &(x[0]), 1,
                                                    beta, &(y[0]), 1))

    L2Benchmark(gemv_trans, "d", cblas_dgemv(CblasColMajor, CblasTrans, N, N,
                                                  alpha, &(A[0]), N, &(x[0]), 1,
                                                  beta, &(y[0]), 1))

    L2Benchmark(ger, "d", cblas_dger(CblasColMajor, N, N, alpha, &(x[0]), 1,
                                     &(y[0]), 1, &(A[0]), N))

    L3Benchmark(gemm_notrans, "d", cblas_dgemm(CblasColMajor, CblasNoTrans, CblasNoTrans, N, N, N,
                                               alpha, &(A[0]), N, &(B[0]), N,
                                               beta, &(C[0]), N))

    L3Benchmark(gemm_transA, "d", cblas_dgemm(CblasColMajor, CblasTrans, CblasNoTrans, N, N, N,
                                              alpha, &(A[0]), N, &(B[0]), N,
                                              beta, &(C[0]), N))

    L3Benchmark(gemm_transB, "d", cblas_dgemm(CblasColMajor, CblasNoTrans, CblasTrans, N, N, N,
                                              alpha, &(A[0]), N, &(B[0]), N,
                                              beta, &(C[0]), N))

    L3Benchmark(gemm_transAB, "d", cblas_dgemm(CblasColMajor, CblasTrans, CblasTrans, N, N, N,
                                               alpha, &(A[0]), N, &(B[0]), N,
                                               beta, &(C[0]), N))
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
        BenchmarksFloat (BLAS_NAME).run(subroutine, size);
    } else if (type == 'd') {
        BenchmarksDouble(BLAS_NAME).run(subroutine, size);
    }

    return 0;
}
