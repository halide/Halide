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
    std::cout << std::setw(8) << N << " x " << std::setw(4) << N;

    column.str(""); column << delta_t/(1000 * num_iters) << " s";
    std::cout << std::setw(20) << column.str();

    column.str(""); column << num_iters * buffer_size / (1000 * delta_t) << " MB/s\n";
    std::cout << std::setw(20) << column.str();
}

void multiply_small_matrices(const int N, const Func& A, const Func& B, Func& C) {
    Var i("i"), j("j");

    RDom k(0, N, "k");
    C(i, j) += A(i, k) * B(k, j);

    C.bound(i, 0, N).bound(j, 0, N);
    C.update().vectorize(i).unroll(j).unroll(k);

    C.output_buffer()
            .set_min(0,0).set_min(1,0)
            .set_stride(0,1).set_extent(1,N)
            .set_extent(0,N).set_extent(1,N);
}

void test_small_matrix(const int num_iters) {
    const int N = 4;

    ImageParam A_in(Float(32), 2, "A_in"), B_in(Float(32), 2, "B_in");
    A_in.set_min(0,0).set_min(1,0)
            .set_stride(0,1).set_stride(1,N)
            .set_extent(0,N).set_extent(1,N);

    B_in.set_min(0,0).set_min(1,0)
            .set_stride(0,1).set_stride(1,N)
            .set_extent(0,N).set_extent(1,N);

    Var i("i"), j("j");
    Func A("A"), B("B"), C("C");
    A(i, j) = A_in(i, j);
    B(i, j) = B_in(i, j);
    C(i, j) = 0.0f;
    multiply_small_matrices(4, A, B, C);

    Image<float> a(N,N), b(N,N), c(N,N);

    // Fill the inputs with junk
    lambda(i, j, random_float()).realize(a);
    lambda(i, j, random_float()).realize(b);

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
    C.compile_to_lowered_stmt("small_mul.stmt", Text, t);

    // Uncomment to see the generated asm
    C.compile_to_assembly("small_mul.s", Internal::vec<Argument>(A_in, B_in), t);

    A_in.set(a);
    B_in.set(b);

    // Call the routine many times.
    float t1 = current_time();
    for (int i = 0; i < num_iters; i++) {
        // std::cout << "Iter " << std::setw(4) << i << ": " <<  (current_time()-t1)/1000.0 << "\n";
        C.realize(c);
    }
    float t2 = current_time();

    print_results(N, num_iters, "Halide small:", t2 - t1);

#ifdef WITH_EIGEN
    // Allocate some inputs and outputs.
    Eigen::MatrixXf p(N, N), q(N, N), r(N, N);

    // Fill the inputs with junk
    p.setRandom(); q.setRandom();

    // Call the routine many times
    t1 = current_time();
    for (int i = 0; i < num_iters; i++) {
        r = p*q;
    }
    t2 = current_time();

    print_results(N, num_iters, "Eigen small:", t2 - t1);
#endif

}

void test_matrix_multiply(const int N, const int num_iters) {
    ImageParam A_in(Float(32), 2), B_in(Float(32), 2);

    A_in.set_min(0,0).set_min(1,0)
        .set_extent(0,N).set_extent(1,N);

    B_in.set_min(0,0).set_min(1,0)
        .set_extent(0,N).set_extent(1,N);

    Expr size = A_in.width();

    Matrix A(A_in, "A"), B(B_in, "B");
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

    Func prod = static_cast<Func>(C);
    prod.compile_to_lowered_stmt("mat_mul.stmt", Text, t);
    prod.compile_jit(t);

    // Uncomment to see the generated asm
    // C.compile_to_assembly("/dev/stdout", Internal::vec<Argument>(A, B), "");

    A_in.set(a);
    B_in.set(b);

    // Call the routine many times.
    float t1 = current_time();
    for (int i = 0; i < num_iters; i++) {
        // std::cout << "Iter " << std::setw(4) << i << ": " << (current_time()-t1)/1000.0 << "\n";
        prod.realize(c);
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
        // std::cout << "Iter " << std::setw(4) << i << ": " <<  (current_time()-t1)/1000.0 << "\n";
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
    for (int i = 0; i < num_iters; i++) {
        c = a*b;
    }
    float t2 = current_time();

    print_results(N, num_iters, "Eigen:", t2 - t1);
}
#endif

enum {
    test_none = 0,
    test_explicit = 1,
    test_class = 2,
    test_eigen = 4,
    test_small = 8,
    test_all = 7
};

std::vector<std::string> split_string(const std::string &str, const std::string &delim) {
    std::vector<std::string> substrs;

    size_t pos = 0;
    while (pos < str.length()) {
        size_t next_pos = str.find(delim, pos);
        if (next_pos == std::string::npos) {
            next_pos = str.length();
        }
        substrs.push_back(str.substr(pos, next_pos-pos));
        pos = next_pos+1;
    }

    return substrs;
}

const int num_default_sizes = 8;
const int default_sizes[] = {
    16, 32, 64, 128, 256, 512, 1024, 2048
};

void print_usage() {
    std::cout << "performance_matrix_multiply [OPTIONS]\n"
              << "\t--help, -h         \t Print this usage message.\n"
              << "\t--iters, -i <num>  \t Number of iterations of multiply to run for timing.\n"
              << "\t--test, -t <tests> \t Comma separated list of multiplies to test.\n"
              << "\t                   \t Options are {class, explicit, eigen, all}\n"
              << "\t--sizes, -s <nums> \t Comma separated list of numbers N.\n"
              << "\t                   \t Will multiply NxN matrix for each N in the list.\n";
}

int main(int argc, char **argv) {
    int which_test = test_all;
    int num_iters = 10;
    std::vector<int> test_size(default_sizes, default_sizes + num_default_sizes);

    int n = 1;
    while (n < argc) {
        std::string flag = argv[n++];
        if (flag == "-h" || flag == "--help") {
            print_usage();
            return 0;
        }

        if (n >= argc) {
            break;
        } else if (flag == "-t" || flag == "--test") {
            which_test = test_none;
            std::vector<std::string> test_names = split_string(argv[n++], ",");
            for (size_t i = 0; i < test_names.size(); ++i) {
                if (test_names[i] == "explicit") {
                    which_test |= test_explicit;
                } else if (test_names[i] == "class") {
                    which_test |= test_class;
                } else if (test_names[i] == "eigen") {
                    which_test |= test_eigen;
                } else if (test_names[i] == "small") {
                    which_test |= test_small;
                } else if (test_names[i] == "all") {
                    which_test |= test_all;
                }
            }
        } else if (flag == "-i" || flag == "--iters") {
            num_iters = atoi(argv[n++]);
        } else if (flag == "-s" || flag == "--sizes") {
            test_size.clear();
            std::vector<std::string> test_sizes = split_string(argv[n++], ",");
            for (size_t i = 0; i < test_sizes.size(); ++i) {
                test_size.push_back(atoi(test_sizes[i].c_str()));
            }
        }
    }

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

    if (which_test & test_small) {
        test_small_matrix(num_iters);
    }

    if (which_test & test_explicit) {
        for (int i = 0; i < test_size.size(); ++i) {
            test_explicit_multiply(test_size[i], num_iters);
        }
    }

    if (which_test & test_class) {
        for (int i = 0; i < test_size.size(); ++i) {
            test_matrix_multiply(test_size[i], num_iters);
        }
    }

#ifdef WITH_EIGEN
    if (which_test & test_eigen) {
        for (int i = 0; i < test_size.size(); ++i) {
            test_eigen_multiply(test_size[i], num_iters);
        }
    }
#endif

    return 0;
}
