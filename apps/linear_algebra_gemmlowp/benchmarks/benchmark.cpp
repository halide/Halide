//
// Benchmarks Halide implementation of gemmlowp using gemmlowp's benchmarks.
// (Adapted from: https://github.com/google/gemmlowp/blob/master/test/benchmark.cc)
//

#include <cassert>
#include <iomanip>
#include <iostream>
#include <map>
#include <vector>
#include <eight_bit_int_gemm/eight_bit_int_gemm.h>
#include "Halide.h"
#include "halide_blas.h"
#include "../support/benchmark.h"

#define time_it(code)                                       \
    double elapsed = 0;                                     \
    elapsed = 1e6 * benchmark(1, 1, [&]() {code;});         \

namespace gemmlowp_benchmark {

namespace {

Halide::Buffer halide_zero_matrix(int M, int N) {
    Halide::Buffer buff(Halide::type_of<uint8_t>(), M, N);
    uint8_t *A = (uint8_t*)buff.host_ptr();
    for (int i=0; i<M*N; ++i) {
        A[i] = 0;
    }
    return buff;
}

std::vector<uint8_t> gemmlowp_zero_matrix(int M, int N) {
    std::vector<uint8_t> buff(M * N);
    for (int i=0; i<M*N; ++i) {
        buff[i] = 0;
    }
    return buff;
}

} // namespace anonymous

const int a_offset = -75;
const int b_offset = -91;
const int c_offset = 74980;
const int c_mult_int = 123;
const int c_shift = 20;

enum FuncType {
    HALIDE,
    GEMMLOWP,
    EIGEN
};

std::string type_to_string(FuncType type) {
    switch (type) {
    case HALIDE:
        return "halide";
    case GEMMLOWP:
        return "gemmlowp";
    default:
        return "";
    }
}

struct gemm_t {
    int rows, depth, cols;
    gemm_t() : rows(0), depth(0), cols(0) {}
    gemm_t(int r, int d, int c) : rows(r), depth(d), cols(c) {}
};

bool operator<(const gemm_t& a, const gemm_t& b) {
    return a.rows < b.rows ||
           (a.rows <= b.rows &&
           (a.depth < b.depth || (a.depth <= b.depth && (a.cols < b.cols))));
}

double time_for_gemms_halide(const std::vector<gemm_t>& gemms) {
    typedef Halide::Buffer Matrix;
    double total_elapsed = 0;
    for (size_t i = 0; i < gemms.size(); ++i) {
        Matrix A = halide_zero_matrix(gemms[i].rows, gemms[i].depth);
        Matrix B = halide_zero_matrix(gemms[i].depth, gemms[i].cols);
        Matrix C = halide_zero_matrix(gemms[i].rows, gemms[i].cols);

        time_it(halide_igemm(false, false, false, A.raw_buffer(), a_offset,
                             B.raw_buffer(), b_offset, C.raw_buffer(),
                             c_offset, c_mult_int, c_shift))
        total_elapsed += elapsed;

    }
    return total_elapsed;
}

double time_for_gemms_gemmlowp(const std::vector<gemm_t>& gemms) {
    typedef std::vector<uint8_t> Matrix;
    double total_elapsed = 0;
    for (size_t i = 0; i < gemms.size(); ++i) {
        Matrix A = gemmlowp_zero_matrix(gemms[i].rows, gemms[i].depth);
        Matrix B = gemmlowp_zero_matrix(gemms[i].depth, gemms[i].cols);
        Matrix C = gemmlowp_zero_matrix(gemms[i].rows, gemms[i].cols);


        time_it(gemmlowp::eight_bit_int_gemm::EightBitIntGemm(
                    false, false, false, gemms[i].rows, gemms[i].cols, gemms[i].depth,
                    &(A[0]), a_offset, gemms[i].rows, &(B[0]), b_offset, gemms[i].depth,
                    &(C[0]), c_offset, c_mult_int, c_shift, gemms[i].rows,
                    gemmlowp::eight_bit_int_gemm::BitDepthSetting::A8B8))
        total_elapsed += elapsed;
    }
    return total_elapsed;
}

double time_for_gemms(const std::vector<gemm_t>& gemms, FuncType type) {
    switch (type) {
    case HALIDE:
        return time_for_gemms_halide(gemms);
    case GEMMLOWP:
        return time_for_gemms_gemmlowp(gemms);
    default:
        assert(false);
    }
}

void benchmark_general(FuncType type) {
    std::map<gemm_t, std::vector<double>> benchmark_results;

    std::vector<gemm_t> benchmark_gemms;
    benchmark_gemms.emplace_back(10, 10, 10);
    benchmark_gemms.emplace_back(20, 20, 20);
    benchmark_gemms.emplace_back(30, 30, 30);
    benchmark_gemms.emplace_back(40, 40, 40);
    benchmark_gemms.emplace_back(50, 50, 50);
    benchmark_gemms.emplace_back(60, 60, 60);
    benchmark_gemms.emplace_back(64, 256, 147);
    benchmark_gemms.emplace_back(100, 100, 1);
    benchmark_gemms.emplace_back(100, 100, 100);
    benchmark_gemms.emplace_back(100, 1000, 100);
    benchmark_gemms.emplace_back(1000, 1000, 1);
    benchmark_gemms.emplace_back(1000, 1000, 10);
    benchmark_gemms.emplace_back(1000, 1000, 100);
    benchmark_gemms.emplace_back(1000, 1000, 1000);

    const int repeat = 2;

    for (int r = 0; r < repeat + 1; ++r) {
        for (const auto& gemm : benchmark_gemms) {
            std::vector<gemm_t> unique_gemm;
            unique_gemm.push_back(gemm);
            double elapsed = time_for_gemms(unique_gemm, type);
            if (r > 0) { // Ignore the first repetition
                benchmark_results[gemm].emplace_back(elapsed);
            }
        }
    }

    std::cout << std::endl;
    for (auto b : benchmark_results) {
        sort(b.second.begin(), b.second.end());
        double elapsed = b.second.back();
        int M = b.first.rows;
        int K = b.first.depth;
        int N = b.first.rows;
        double gflops = 2.0 * K * M * N * 1e-3 / elapsed;
        std::cout << std::setw(8) << type_to_string(type)
                  << std::setw(8) << std::to_string(M)
                  << std::setw(8) << std::to_string(K)
                  << std::setw(8) << std::to_string(N)
                  << std::setw(20) << std::to_string(elapsed)
                  << std::setw(20) << std::to_string(gflops)
                  << std::endl;
    }
    std::cout << std::endl;
}

void benchmark_gemm_sizes(const std::vector<gemm_t>& gemms, FuncType type) {
    std::vector<float> gemm_times;
    const int iters = 30;
    std::cout << "Running " << type_to_string(type) << " for " << iters
              << " iterations..." << std::endl;

    for (int i = 0; i < iters; ++i) {
        gemm_times.push_back(time_for_gemms(gemms, type));
    }

    std::sort(gemm_times.begin(), gemm_times.end());

    double sum_gemm_times = 0;
    double sum_gemm_times_trimmed = 0;
    int count_gemm_times_trimmed = 0;
    const float trim_ratio = 0.25;
    const size_t count_trimmed = gemm_times.size() * trim_ratio;
    double sum_gemm_times_best = 0;
    int count_gemm_times_best = 0;
    const float best_ratio = 0.1;
    const size_t count_best = gemm_times.size() * best_ratio;

    for (size_t i = 0; i < gemm_times.size(); i++) {
        sum_gemm_times += gemm_times[i];
        if (i >= count_trimmed && i < gemm_times.size() - count_trimmed) {
            sum_gemm_times_trimmed += gemm_times[i];
            count_gemm_times_trimmed++;
        }
        if (i < count_best) {
            sum_gemm_times_best += gemm_times[i];
            count_gemm_times_best++;
        }
    }

    const double min_latency = gemm_times.front();
    const double max_latency = gemm_times.back();
    const double mean_latency = sum_gemm_times / gemm_times.size();
    const double trimmed_mean_latency =
        sum_gemm_times_trimmed / count_gemm_times_trimmed;
    const double best_mean_latency = sum_gemm_times_best / count_gemm_times_best;

    std::cout << "Graph latency (over " << gemm_times.size()
              << " iterations):" << std::endl;
    std::cout << "  Best:             " << min_latency << "s" << std::endl;
    std::cout << "  Worst:            " << max_latency << "s" << std::endl;
    std::cout << "  Mean:             " << mean_latency << "s" << std::endl;
    std::cout << "  " << 100 * trim_ratio
              << "% trimmed mean: " << trimmed_mean_latency << "s" << std::endl;
    std::cout << "  Mean of " << 100 * best_ratio
              << "% best: " << best_mean_latency << "s" << std::endl;
}

void benchmark_googlenet(FuncType type) {
    // These are the m, n, k sizes for a typical GoogLeNet.
    const int googlenet_gemm_sizes[] = {
        12544, 64,  147, 3136, 64,   64,   3136, 192,  576,  784, 64,   192,
        784,   96,  192, 784,  128,  864,  784,  16,   192,  784, 32,   400,
        784,   32,  192, 784,  128,  256,  784,  128,  256,  784, 192,  1152,
        784,   32,  256, 784,  96,   800,  784,  64,   256,  196, 192,  480,
        196,   96,  480, 196,  204,  864,  196,  16,   480,  196, 48,   400,
        196,   64,  480, 196,  160,  508,  196,  112,  508,  196, 224,  1008,
        196,   24,  508, 196,  64,   600,  196,  64,   508,  196, 128,  512,
        196,   128, 512, 196,  256,  1152, 196,  24,   512,  196, 64,   600,
        196,   64,  512, 196,  112,  512,  196,  144,  512,  196, 288,  1296,
        196,   32,  512, 196,  64,   800,  196,  64,   512,  196, 256,  528,
        196,   160, 528, 196,  320,  1440, 196,  32,   528,  196, 128,  800,
        196,   128, 528, 49,   256,  832,  49,   160,  832,  49,  320,  1440,
        49,    48,  832, 49,   128,  1200, 49,   128,  832,  49,  384,  832,
        49,    192, 832, 49,   384,  1728, 49,   48,   832,  49,  128,  1200,
        49,    128, 832, 16,   128,  508,  1,    1024, 2048, 1,   1008, 1024,
        16,    128, 528, 1,    1024, 2048, 1,    1008, 1024, 1,   1008, 1024,
    };
    assert(sizeof(googlenet_gemm_sizes) % (3 * sizeof(googlenet_gemm_sizes[0])) == 0);
    const std::size_t num_googlenet_gemms =
        sizeof(googlenet_gemm_sizes) / (3 * sizeof(googlenet_gemm_sizes[0]));

    std::vector<gemm_t> googlenet_gemms(num_googlenet_gemms);
    for (std::size_t i = 0; i < num_googlenet_gemms; i++) {
        googlenet_gemms[i].rows = googlenet_gemm_sizes[3 * i + 1];
        googlenet_gemms[i].depth = googlenet_gemm_sizes[3 * i + 2];
        googlenet_gemms[i].cols = googlenet_gemm_sizes[3 * i + 0];
    }

    benchmark_gemm_sizes(googlenet_gemms, type);
}

void benchmark_small_model(FuncType type) {
    // These are the m, n, k sizes for a small model with large batches.
    const int small_model_gemm_sizes[] = {
        29232, 16, 25, 7308, 6, 400, 203, 3002, 216,
    };
    assert(sizeof(small_model_gemm_sizes) % (3 * sizeof(small_model_gemm_sizes[0])) == 0);
    const std::size_t num_small_model_gemms =
        sizeof(small_model_gemm_sizes) / (3 * sizeof(small_model_gemm_sizes[0]));

    std::vector<gemm_t> small_model_gemms(num_small_model_gemms);
    for (std::size_t i = 0; i < num_small_model_gemms; i++) {
        small_model_gemms[i].rows = small_model_gemm_sizes[3 * i + 1];
        small_model_gemms[i].depth = small_model_gemm_sizes[3 * i + 2];
        small_model_gemms[i].cols = small_model_gemm_sizes[3 * i + 0];
    }

    benchmark_gemm_sizes(small_model_gemms, type);
}

void benchmark_all() {
    {
        std::cout << "Benchmarking small model GEMMs..." << std::endl;
        benchmark_small_model(GEMMLOWP);
        benchmark_small_model(HALIDE);
    }

    {
        std::cout << "Benchmarking typical GoogLeNet GEMMs..." << std::endl;
        benchmark_googlenet(GEMMLOWP);
        benchmark_googlenet(HALIDE);
    }

    {
        std::cout << "Benchmarking default mode (typically multi-threaded)..."
                  << std::endl;
        benchmark_general(GEMMLOWP);
        benchmark_general(HALIDE);
    }
}

} // namespace gemmlowp_benchmark

int main() {
    gemmlowp_benchmark::benchmark_all();
}