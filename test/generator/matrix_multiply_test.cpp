#include <math.h>
#include <stdio.h>
#include <assert.h>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>

#include <HalideRuntime.h>

#include "matrix_multiply_func.h"
#include "matrix_multiply_class.h"
#include "static_image.h"
#include "../performance/clock.h"

#ifdef WITH_EIGEN
# include <Eigen/Dense>
#endif

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

void halide_func_multiply(const int N, const int num_iters) {
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
    float t1 = current_time();
    for (int i = 0; i < num_iters; i++) {
        matrix_multiply_func(A, B, C);
    }
    float t2 = current_time();

    print_results(N, num_iters, "Halide Func:", t2 - t1);
}

void halide_class_multiply(const int N, const int num_iters) {
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
    float t1 = current_time();
    for (int i = 0; i < num_iters; i++) {
        matrix_multiply_class(A, B, C);
    }
    float t2 = current_time();

    print_results(N, num_iters, "Halide Class:", t2 - t1);
}

int main(int argc, char **argv) {
    const int num_sizes = 8;
    const int num_iters = 100;
    const int sizes[] = {
        16, 32, 64, 128, 256, 0 //512, 1024, 0
    };

    std::cout << std::setw(25) << "Implementation"
              << std::setw(15) << "Matrix Size"
              << std::setw(20) << "Average Runtime"
              << std::setw(20) << "Data Throughput\n";
    for (int i = 0; i < 80; ++i ) std::cout << '='; std::cout << "\n";
    for (int i = 0; sizes[i] != 0; ++i) {
        halide_func_multiply(sizes[i], num_iters);
        halide_class_multiply(sizes[i], num_iters);
#ifdef WITH_EIGEN
        eigen_multiply(sizes[i], num_iters);
#endif
        for (int i = 0; i < 80; ++i ) std::cout << '-'; std::cout << "\n";
    }

    printf("Success!\n");
    return 0;
}
