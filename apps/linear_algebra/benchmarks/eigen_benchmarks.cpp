// USAGE: eigen_benchmarks <subroutine> <size>
//
// Benchmarks BLAS subroutines using Eigen's implementation. Will
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
#include <string>
#include <Eigen/Eigen>
#include "clock.h"
#include "macros.h"

template<class T>
std::string type_name();

template<>
std::string type_name<float>() {return "s";}

template<>
std::string type_name<double>() {return "d";}

struct BenchmarksBase {
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

template<class T>
struct Benchmarks : BenchmarksBase {
    typedef T Scalar;
    typedef Eigen::Matrix<T, Eigen::Dynamic, 1> Vector;
    typedef Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic> Matrix;

    Scalar random_scalar() {
        Vector x(1);
        x.setRandom();
        return x[0];
    }

    Vector random_vector(int N) {
        Vector x(N);
        x.setRandom();
        return x;
    }

    Matrix random_matrix(int N) {
        Matrix A(N, N);
        A.setRandom();
        return A;
    }

    Benchmarks(std::string n) : name(n) {}

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

    Scalar result;

    L1Benchmark(copy, type_name<T>(), y = x);
    L1Benchmark(scal, type_name<T>(), x = alpha * x);
    L1Benchmark(axpy, type_name<T>(), y = alpha * x + y);
    L1Benchmark(dot,  type_name<T>(), result = x.dot(y));
    L1Benchmark(asum, type_name<T>(), result = x.array().abs().sum());

    L2Benchmark(gemv_notrans, type_name<T>(), y = alpha * A * x + beta * y);
    L2Benchmark(gemv_trans,   type_name<T>(), y = alpha * A.transpose() * x + beta * y);
    L2Benchmark(ger,          type_name<T>(), A = alpha * x * y.transpose() + A);

    L3Benchmark(gemm_notrans, type_name<T>(), C = alpha * A * B + beta * C);
    L3Benchmark(gemm_transA, type_name<T>(), C = alpha * A.transpose() * B + beta * C);
    L3Benchmark(gemm_transB, type_name<T>(), C = alpha * A * B.transpose() + beta * C);
    L3Benchmark(gemm_transAB, type_name<T>(), C = alpha * A.transpose() * B.transpose() + beta * C);

  private:
    std::string name;
};

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cout << "USAGE: eigen_benchmarks <subroutine> <size>\n";
        return 0;
    }

    std::string subroutine = argv[1];
    char type = subroutine[0];
    int  size = std::stoi(argv[2]);

    subroutine = subroutine.substr(1);
    if (type == 's') {
        Benchmarks<float> ("Eigen").run(subroutine, size);
    } else if (type == 'd') {
        Benchmarks<double>("Eigen").run(subroutine, size);
    }

    return 0;
}
