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
        if (benchmark == "gemv") {
            this->bench_gemv(size);
        }
    }

    virtual void bench_gemv(int N) =0;
};

struct BenchmarksFloat : public BenchmarksBase<float> {
    BenchmarksFloat(std::string n, int iters) :
            BenchmarksBase(n, iters)
    {}

    virtual void bench_gemv(int N) {
        Scalar alpha = randomScalar();
        Scalar beta = randomScalar();
        std::unique_ptr<Vector> x(randomVector(N));
        std::unique_ptr<Vector> y(randomVector(N));
        std::unique_ptr<Matrix> A(randomMatrix(N));

        double start = current_time();
        for (int i = 0; i < num_iters; ++i) {
            halide_sgemv(false, alpha, A->raw_buffer(), x->raw_buffer(), beta, y->raw_buffer());
        }
        double end = current_time();
        double elapsed = end - start;

        std::cout << std::setw(10) << name + ":"
                  << std::setw(25) << std::to_string(elapsed) + "(ms)"
                  << std::setw(25) << std::to_string(N * 1000 / elapsed) + "(items/s)"
                  << std::endl;
    }
};

struct BenchmarksDouble : public BenchmarksBase<double> {
    BenchmarksDouble(std::string n, int iters) :
            BenchmarksBase(n, iters)
    {}

    virtual void bench_gemv(int N) {
        Scalar alpha = randomScalar();
        Scalar beta = randomScalar();
        std::unique_ptr<Vector> x(randomVector(N));
        std::unique_ptr<Vector> y(randomVector(N));
        std::unique_ptr<Matrix> A(randomMatrix(N));

        double start = current_time();
        for (int i = 0; i < num_iters; ++i) {
            halide_dgemv(false, alpha, A->raw_buffer(), x->raw_buffer(), beta, y->raw_buffer());
        }
        double end = current_time();
        double elapsed = end - start;

        std::cout << std::setw(10) << name + ":"
                  << std::setw(25) << std::to_string(elapsed) + "(ms)"
                  << std::setw(25) << std::to_string(N * 1000 / elapsed) + "(items/s)"
                  << std::endl;
    }
};

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cout << "USAGE: halide_benchmarks <subroutine> <size>\n";
        return 0;
    }

    BenchmarksFloat ("Halide::sgemv", 1000).run(argv[1], std::stoi(argv[2]));
    BenchmarksDouble("Halide::dgemv", 1000).run(argv[1], std::stoi(argv[2]));

    return 0;
}
