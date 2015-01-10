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
        if (benchmark == "gemv") {
            bench_gemv(size);
        }
    }

    void bench_gemv(int N) {
        Scalar alpha = randomScalar();
        Scalar beta = randomScalar();
        Vector x = randomVector(N);
        Vector y = randomVector(N);
        Matrix A = randomMatrix(N);

        double start = current_time();
        for (int i = 0; i < num_iters; ++i) {
            y = alpha * A * x + beta * y;
        }
        double end = current_time();
        double elapsed = end - start;

        std::cout << std::setw(10) << name + ":"
                  << std::setw(25) << std::to_string(elapsed) + "(ms)"
                  << std::setw(25) << std::to_string(N * 1000 / elapsed) + "(items/s)"
                  << std::endl;
    }

  private:
    std::string name;
    int num_iters;
};

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cout << "USAGE: eigen_benchmarks <subroutine> <size>\n";
        return 0;
    }

    Benchmarks<float> ("Eigen::sgemv", 1000).run(argv[1], std::stoi(argv[2]));
    Benchmarks<double>("Eigen::dgemv", 1000).run(argv[1], std::stoi(argv[2]));

    return 0;
}
