#include <iomanip>
#include <iostream>
#include <sstream>

#include <Halide.h>
#include <stdio.h>
#include "clock.h"

#ifdef WITH_EIGEN
# include <Eigen/Dense>
#endif

using namespace Halide;

void print_results(const int N, const int num_iters, const std::string &result, const double delta_t) {
    const size_t buffer_size = N * N * sizeof(float);
    std::stringstream column;

    std::cout << std::setw(25) << result;

    column.str(""); column << N << " x " << N;
    std::cout << std::setw(15) << column.str();

    column.str(""); column << delta_t/(1000 * num_iters) << " s";
    std::cout << std::setw(20) << column.str();

    column.str(""); column << num_iters * buffer_size / (1000 * delta_t) << " MB/s\n";
    std::cout << std::setw(20) << column.str();
}

void test_matrix_multiply(const int N, const int num_iters) {
    ImageParam A_in(Float(32), 2), B_in(Float(32), 2);

    Expr size = A_in.width();

    Matrix A(A_in), B(B_in);
    Matrix C = A * B;

    //Var tx("tx"), ty("ty"), ttx("ttx"), tty("tty");
    Var x("x"), y("y");
    // C.function().parallel(y, 16).vectorize(x, 8);

    // Allocate some inputs and outputs.
    Image<float> a(N,N), b(N,N), c(N,N);

    // Fill the inputs with junk
    lambda(x, y, random_float()).realize(a);
    lambda(x, y, random_float()).realize(b);

    // Note we don't specialize on the matrix size, even though
    // it's known at compile time in this case. We don't do this
    // for Eigen either.
    Target t = get_host_target();
    t.set_feature(Target::NoAsserts);
    t.set_feature(Target::NoBoundsQuery);
    C.function().compile_jit(t);
    C.function().compile_to_lowered_stmt("mat_mul.stmt", Text, t);

    // Uncomment to see the generated asm
    // C.compile_to_assembly("/dev/stdout", Internal::vec<Argument>(A, B), "");

    A_in.set(a);
    B_in.set(b);

    // Call the routine many times.
    float t1 = current_time();
    for (int i = 0; i < num_iters; i++) {
        C.function().realize(c);
    }
    float t2 = current_time();

    print_results(N, num_iters, "Halide matrix:", t2 - t1);
}

void test_explicit_multiply(const int N, const int num_iters) {
    ImageParam A(Float(32), 2), B(Float(32), 2);

    Expr size = A.width();
    Func dot, C;

    Var ti("ti"), tj("tj"), tti("tti"), ttj("ttj");
    Var i("i"), j("j");

    // Pretranspose B so we can take dot products of rows.
    Func Bt;
    Bt(i, j) = B(j, i);

    // Compute a dot product of a row in A and a row in Bt. First
    // accumulate in vectors, and then accumulate the lanes in
    // scalar code at the end. This assumes that S is a multiply
    // of vec_size.
    const int vec_size = 8;

    RDom sum_vecs(0, size/vec_size);
    Var k("k");
    dot(k, i, j) += A(sum_vecs*vec_size + k, i) * Bt(sum_vecs*vec_size + k, j);

    RDom sum_lanes(0, vec_size);
    C(i, j) = sum(dot(sum_lanes, i, j));

    // Compute the result in 16 x 16 tiles, with each row of tiles
    // on a separate core. Split each tile recursively into four
    // 8x8 sub-tiles to compute the dot products.
    C.tile(i, j, ti, tj, i, j, 16, 16).tile(i, j, tti, ttj, i, j, 8, 8).parallel(tj);

    // Compute the dot product per sub-tile. Vectorize it, and
    // unroll across the sub-tile.
    dot.compute_at(C, tti).vectorize(k);
    dot.update()
            .reorder(k, i, j, sum_vecs).vectorize(k)
            .unroll(i).unroll(j);

    // Compute B transpose per-core as needed in 16x16 tiles.
    Bt.compute_at(C, tj).tile(i, j, ti, tj, i, j, 16, 16);

    // Allocate some inputs and outputs.
    Image<float> a(N, N), b(N, N), c(N, N);
    // Fill the inputs with junk
    lambda(i, j, sin(i+j)).realize(a);
    lambda(i, j, cos(i-j)).realize(b);

    // Note we don't specialize on the matrix size, even though
    // it's known at compile time in this case. We don't do this
    // for Eigen either.
    C.compile_jit();

    // Note we don't specialize on the matrix size, even though
    // it's known at compile time in this case. We don't do this
    // for Eigen either.
    Target t = get_host_target();
    t.set_feature(Target::NoAsserts);
    t.set_feature(Target::NoBoundsQuery);
    C.compile_jit(t);
    C.compile_to_lowered_stmt("exp_mul.stmt", Text, t);

    // Uncomment to see the generated asm
    // C.compile_to_assembly("/dev/stdout", Internal::vec<Argument>(A, B), "");

    A.set(a);
    B.set(b);

    // Call the routine many times.
    float t1 = current_time();
    for (int i = 0; i < num_iters; i++) {
        C.realize(c);
    }
    float t2 = current_time();

    print_results(N, num_iters, "Halide explicit:", t2 - t1);
}

#ifdef WITH_EIGEN
void test_eigen_multiply(const int N, const int num_iters) {
    // Allocate some inputs and outputs.
    Eigen::MatrixXf a(N, N), b(N, N), c(N, N);

    // Fill the inputs with junk
    a.setRandom(); b.setRandom();

    // Call the routine many times
    float t1 = current_time();
    for (int i = 0; i < 10; i++) {
        c = a*b;
    }
    float t2 = current_time();

    print_results(N, num_iters, "Eigen:", t2 - t1);
}
#endif

int main(int argc, char **argv) {
    const int num_iters = 1;
    const int test_size[] = {
        //16, 0
        16, 32, 64, 128, 256, 512, 1024, 2048, 0
    };

    // const int test_size[] = {
    //     2, 3, 4, // Small matrices
    //     10, 50, 100, 500,
    //     1000, 10000, 100000,
    //     0
    // };

    std::cout << std::setw(25) << "Implementation"
              << std::setw(15) << "Matrix Size"
              << std::setw(20) << "Average Runtime"
              << std::setw(20) << "Data Throughput\n";
    for (int i = 0; i < 80; ++i ) std::cout << '-'; std::cout << "\n";

    for (int i = 0; test_size[i] != 0; ++i) {
        test_explicit_multiply(test_size[i], num_iters);
    }

    for (int i = 0; test_size[i] != 0; ++i) {
        test_matrix_multiply(test_size[i], num_iters);
    }

#ifdef WITH_EIGEN
    for (int i = 0; test_size[i] != 0; ++i) {
        test_eigen_multiply(test_size[i], num_iters);
    }
#endif

    return 0;
}
