#include <math.h>
#include <stdio.h>
#include <assert.h>
#include <iomanip>
#include <iostream>
#include <iomanip>
#include <random>
#include <sstream>

#include <HalideRuntime.h>

#include "matrix_multiply_class.h"
#include "matrix_multiply_explicit.h"
#include "static_image.h"
#include "performance/clock.h"

#ifdef WITH_BLAS
# include <cblas.h>
#endif

#ifdef WITH_EIGEN
# include <Eigen/Dense>
#endif

enum MatrixMultiplyAlgorithm {
    kClassMultiply,
    kExplicitMultiply
};

void print_matrix(const Image<float>& A, bool transpose=false) {
    if (!transpose) {
        for (int i = 0; i < A.width(); ++i) {
            for (int j = 0; j < A.height(); ++j) {
                std::cout << std::setprecision(3) << std::setw(10) << A(i, j);
            }

            std::cout << std::endl;
        }
    } else {
        for (int j = 0; j < A.height(); ++j) {
            for (int i = 0; i < A.width(); ++i) {
                std::cout << std::setprecision(3) << std::setw(10) << A(i, j);
            }

            std::cout << std::endl;
        }
    }
}

void multiply(const Image<float>& A, const Image<float>& B, Image<float>& C) {
    for (int j = 0; j < B.height(); ++j) {
        for (int i = 0; i < A.width(); ++i) {
            C(i, j) = 0.0f;

            for (int k = 0; k < A.height(); ++k) {
                C(i, j) += A(i, k) * B(k, j);
            }
        }
    }
}

bool check_multiply(const Image<float>& A, const Image<float>& B, const Image<float>& C, const float tolerance = 1e-4) {
    Image<float> result(C.width(), C.height());
    multiply(A, B, result);

    double abs_diff = 0.0;
    for (int i = 0; i < C.width(); ++i) {
        for (int j = 0; j < C.height(); ++j) {
            abs_diff += abs(C(i, j) - result(i, j));
        }
    }

    double avg_diff = abs_diff / (C.width() * C.height());
    if (avg_diff > tolerance) {
        std::cerr << "matrix comparison failed! avg. diff = " << avg_diff << "\n";
        std::cerr << "A:\n"; print_matrix(A);
        std::cerr << "B:\n"; print_matrix(B);
        std::cerr << "expected:\n"; print_matrix(result);
        std::cerr << "actual:\n"; print_matrix(C);
        return false;
    }
    return true;
}

void print_results(const int N, const int num_iters, const std::string &result, const double delta_t) {
    const size_t buffer_size = N * N * sizeof(float);
    std::stringstream column;

    std::cout << std::setw(25) << result;
    std::cout << std::setw(8) << N << " x " << std::setw(4) << N;

    column.str(""); column << delta_t/(1000 * num_iters) << " s";
    std::cout << std::setw(20) << column.str();

    column.str(""); column << num_iters * buffer_size / (1000 * delta_t) << " MB/s\n";
    std::cout << std::setw(20) << column.str();
}

#ifdef WITH_BLAS
void blas_multiply(const int N, const int num_iters) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> rand(0, 1);

    // Allocate some inputs and outputs.
    float *A = new float[N*N];
    float *B = new float[N*N];
    float *C = new float[N*N];
    float alpha = 1.0f;
    float beta = 1.0f;
    int lda = N;
    int ldb = N;
    int ldc = N;

    // Fill the inputs with junk
    for (int i = 0; i < N*N; ++i) {
        A[i] = rand(gen);
        B[i] = rand(gen);
        C[i] = 0;
    }

    // Call the routine many times
    float t1 = current_time();
    for (int i = 0; i < num_iters; i++) {
        cblas_sgemm(CblasColMajor, CblasNoTrans, CblasNoTrans,
                    N, N, N, alpha, A, lda, B, ldb, beta, C, ldc);
    }
    float t2 = current_time();

    print_results(N, num_iters, "Blas Matrix:", t2 - t1);
}
#endif

#ifdef WITH_EIGEN
void eigen_multiply(const int N, const int num_iters) {
    // Allocate some inputs and outputs.
    Eigen::MatrixXf A(N, N), B(N, N), C(N, N);

    // Fill the inputs with junk
    A.setRandom(); B.setRandom();

    // Call the routine many times
    float t1 = current_time();
    for (int i = 0; i < num_iters; i++) {
        C = A * B;
    }
    float t2 = current_time();

    print_results(N, num_iters, "Eigen Matrix:", t2 - t1);
}
#endif

void halide_multiply(const int N, const int num_iters, const MatrixMultiplyAlgorithm algorithm, std::string label) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> rand(0, 1);

    // Allocate some inputs and outputs.
    Image<float> A(N,N), B(N,N), C(N,N);
    for (int j = 0; j < N; ++j) {
        for (int i = 0; i < N; ++i) {
            A(i, j) = rand(gen);
            B(i, j) = rand(gen);
        }
    }

    // Call the routine many times.
    float t1, t2;
    switch(algorithm) {
        case kClassMultiply:
            t1 = current_time();
            for (int i = 0; i < num_iters; i++) {
                matrix_multiply_class(A, B, C);
            }
            t2 = current_time();
        case kExplicitMultiply:
            t1 = current_time();
            for (int i = 0; i < num_iters; i++) {
                matrix_multiply_explicit(A, B, C);
            }
            t2 = current_time();
    }

    print_results(N, num_iters, "Halide " + label + ":", t2 - t1);
}

bool test_correctness(const int N) {
    // Allocate some inputs and outputs.
    Image<float> A(N,N), B(N,N), C(N,N);
    for (int j = 0; j < N; ++j) {
        for (int i = 0; i < N; ++i) {
            float x = ((float)i)/(N-1);
            float y = ((float)j)/(N-1);
            A(i, j) = sin(0.25*N*x) * sin(0.1*N*y);
            B(i, j) = cos(0.05*N*x) + cos(0.33*N*y);
        }
    }

    bool correct = true;
    Image<float> C0(N,N);
    matrix_multiply_class(A, B, C0);
    if (!check_multiply(A, B, C0)) {
        std::cout << "Class multiply is not correct!\n";
        correct = false;
    }

    correct = true;
    Image<float> C1(N,N);
    matrix_multiply_explicit(A, B, C1);
    if (!check_multiply(A, B, C1)) {
        std::cout << "Explicit multiply is not correct!\n";
        correct = false;
    }

    return correct;
}

int main(int argc, char **argv) {
    const int num_sizes = 8;
    const int num_iters = 100;
    const int sizes[] = {
        // /*16,*/ 32, 64, 128, 256, 512, 1024, 0
        9, 20, 33, 72, 150, 300, 519, 0
    };

    for (int i = 0; i < 5; ++i) {
        if (!test_correctness(sizes[i])) {
            std::cerr << "Failure!\n";
            return -1;
        }
     }

    std::cout << std::setw(25) << "Implementation"
              << std::setw(15) << "Matrix Size"
              << std::setw(20) << "Average Runtime"
              << std::setw(20) << "Data Throughput\n";
    for (int i = 0; i < 80; ++i ) std::cout << '='; std::cout << "\n";
    for (int i = 0; sizes[i] != 0; ++i) {
        halide_multiply(sizes[i], num_iters, kClassMultiply, "class");
        halide_multiply(sizes[i], num_iters, kExplicitMultiply, "explicit");
#ifdef WITH_EIGEN
        eigen_multiply(sizes[i], num_iters);
#endif
#ifdef WITH_BLAS
        blas_multiply(sizes[i], num_iters);
#endif
        for (int i = 0; i < 80; ++i ) std::cout << '-'; std::cout << "\n";
    }

    printf("Success!\n");
    return 0;
}
