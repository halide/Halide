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

#include "HalideBuffer.h"
#include "clock.h"
#include "halide_blas.h"
#include "macros.h"
#include <iomanip>
#include <iostream>
#include <random>
#include <string>

template<class T>
struct BenchmarksBase {
    typedef T Scalar;
    typedef Halide::Runtime::Buffer<T, 1> Vector;
    typedef Halide::Runtime::Buffer<T, 2> Matrix;

    std::random_device rand_dev;
    std::default_random_engine rand_eng{rand_dev()};

    std::string name;

    Scalar random_scalar() {
        std::uniform_real_distribution<T> uniform_dist(0.0, 1.0);
        return uniform_dist(rand_eng);
    }

    Vector random_vector(int N) {
        Vector buff(N);
        Scalar *x = (Scalar *)buff.data();
        for (int i = 0; i < N; ++i) {
            x[i] = random_scalar();
        }
        return buff;
    }

    Matrix random_matrix(int N) {
        Matrix buff(N, N);
        Scalar *A = (Scalar *)buff.data();
        for (int i = 0; i < N * N; ++i) {
            A[i] = random_scalar();
        }
        return buff;
    }

    BenchmarksBase(std::string n)
        : name(n) {
    }

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
            bench_gemv_notrans(size);
        } else if (benchmark == "gemv_trans") {
            bench_gemv_trans(size);
        } else if (benchmark == "ger") {
            bench_ger(size);
        } else if (benchmark == "gemm_notrans") {
            bench_gemm_notrans(size);
        } else if (benchmark == "gemm_transA") {
            bench_gemm_transA(size);
        } else if (benchmark == "gemm_transB") {
            bench_gemm_transB(size);
        } else if (benchmark == "gemm_transAB") {
            bench_gemm_transAB(size);
        }
    }

    virtual void bench_copy(int N) = 0;
    virtual void bench_scal(int N) = 0;
    virtual void bench_axpy(int N) = 0;
    virtual void bench_dot(int N) = 0;
    virtual void bench_asum(int N) = 0;
    virtual void bench_gemv_notrans(int N) = 0;
    virtual void bench_gemv_trans(int N) = 0;
    virtual void bench_ger(int N) = 0;
    virtual void bench_gemm_notrans(int N) = 0;
    virtual void bench_gemm_transA(int N) = 0;
    virtual void bench_gemm_transB(int N) = 0;
    virtual void bench_gemm_transAB(int N) = 0;
};

struct BenchmarksFloat : public BenchmarksBase<float> {
    BenchmarksFloat(std::string n)
        : BenchmarksBase(n),
          result(Halide::Runtime::Buffer<float, 0>::make_scalar()) {
    }

    Halide::Runtime::Buffer<float, 0> result;

    L1Benchmark(copy, "s", halide_scopy(x.raw_buffer(), y.raw_buffer()));
    L1Benchmark(scal, "s", halide_sscal(alpha, x.raw_buffer()));
    L1Benchmark(axpy, "s", halide_saxpy(alpha, x.raw_buffer(), y.raw_buffer()));
    L1Benchmark(dot, "s", halide_sdot(x.raw_buffer(), y.raw_buffer(), result.raw_buffer()));
    L1Benchmark(asum, "s", halide_sasum(x.raw_buffer(), result.raw_buffer()));

    L2Benchmark(gemv_notrans, "s", halide_sgemv(false, alpha, A.raw_buffer(), x.raw_buffer(), beta, y.raw_buffer()));

    L2Benchmark(gemv_trans, "s", halide_sgemv(true, alpha, A.raw_buffer(), x.raw_buffer(), beta, y.raw_buffer()));

    L2Benchmark(ger, "s", halide_sger(alpha, x.raw_buffer(), y.raw_buffer(), A.raw_buffer()));

    L3Benchmark(gemm_notrans, "s", halide_sgemm(false, false, alpha, A.raw_buffer(), B.raw_buffer(), beta, C.raw_buffer()));

    L3Benchmark(gemm_transA, "s", halide_sgemm(true, false, alpha, A.raw_buffer(), B.raw_buffer(), beta, C.raw_buffer()));

    L3Benchmark(gemm_transB, "s", halide_sgemm(false, true, alpha, A.raw_buffer(), B.raw_buffer(), beta, C.raw_buffer()));

    L3Benchmark(gemm_transAB, "s", halide_sgemm(true, true, alpha, A.raw_buffer(), B.raw_buffer(), beta, C.raw_buffer()));
};

struct BenchmarksDouble : public BenchmarksBase<double> {
    BenchmarksDouble(std::string n)
        : BenchmarksBase(n),
          result(Halide::Runtime::Buffer<double, 0>::make_scalar()) {
    }

    Halide::Runtime::Buffer<double, 0> result;

    L1Benchmark(copy, "d", halide_dcopy(x.raw_buffer(), y.raw_buffer()));
    L1Benchmark(scal, "d", halide_dscal(alpha, x.raw_buffer()));
    L1Benchmark(axpy, "d", halide_daxpy(alpha, x.raw_buffer(), y.raw_buffer()));
    L1Benchmark(dot, "d", halide_ddot(x.raw_buffer(), y.raw_buffer(), result.raw_buffer()));
    L1Benchmark(asum, "d", halide_dasum(x.raw_buffer(), result.raw_buffer()));

    L2Benchmark(gemv_notrans, "d", halide_dgemv(false, alpha, A.raw_buffer(), x.raw_buffer(), beta, y.raw_buffer()));

    L2Benchmark(gemv_trans, "d", halide_dgemv(true, alpha, A.raw_buffer(), x.raw_buffer(), beta, y.raw_buffer()));

    L2Benchmark(ger, "d", halide_dger(alpha, x.raw_buffer(), y.raw_buffer(), A.raw_buffer()));

    L3Benchmark(gemm_notrans, "d", halide_dgemm(false, false, alpha, A.raw_buffer(), B.raw_buffer(), beta, C.raw_buffer()));

    L3Benchmark(gemm_transA, "d", halide_dgemm(true, false, alpha, A.raw_buffer(), B.raw_buffer(), beta, C.raw_buffer()));

    L3Benchmark(gemm_transB, "d", halide_dgemm(false, true, alpha, A.raw_buffer(), B.raw_buffer(), beta, C.raw_buffer()));

    L3Benchmark(gemm_transAB, "d", halide_dgemm(true, true, alpha, A.raw_buffer(), B.raw_buffer(), beta, C.raw_buffer()));
};

int main(int argc, char *argv[]) {
    if (argc != 3) {
        std::cout << "USAGE: halide_benchmarks <subroutine> <size>\n";
        return 0;
    }

    std::string subroutine = argv[1];
    char type = subroutine[0];
    int size = std::stoi(argv[2]);

    subroutine = subroutine.substr(1);
    if (type == 's') {
        BenchmarksFloat("Halide").run(subroutine, size);
    } else if (type == 'd') {
        BenchmarksDouble("Halide").run(subroutine, size);
    }

    return 0;
}
