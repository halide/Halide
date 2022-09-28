#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <iostream>

#include "HalideBuffer.h"
#include "HalideRuntime.h"

#include "conv_nn_llvm.h"
#include "conv_nn_halide.h"
#include "conv_nn_pitchfork.h"
#include "conv_nn_rake.h"

#include "halide_benchmark.h"
#include "halide_image_io.h"

using namespace Halide::Tools;

int main(int argc, char **argv) {
    if (argc != 6) {
        printf("Usage: ./run C N M B timing_iterations\n");
        return -1;
    }

    // const int C = atoi(argv[1]);
    // const int N = atoi(argv[2]);
    // const int M = atoi(argv[3]);
    // const int B = atoi(argv[4]);
    const int timing_iterations = atoi(argv[5]);

    const int width = 128;
    const int height = 128;

    // int pagesize = sysconf(_SC_PAGE_SIZE);
    // if (pagesize == -1) return -1;
    unsigned char *input;
    unsigned char *output;
    posix_memalign((void **)&input, 1 << 7, width*height*sizeof(unsigned char));
    posix_memalign((void **)&output, 1 << 7, width*height*4*sizeof(unsigned char));
    int32_t* bias;
    posix_memalign((void **)&bias, 1 << 7, width*height*sizeof(int32_t));

    halide_dimension_t c_dim{ 0, 1024, 1 };
    halide_dimension_t x_dim{ 0, width / 32, 128 };
    halide_dimension_t y_dim{ 0, height / 32, 128 * (width / 32) };
    halide_dimension_t b_dim{ 0, 1, 128 * (width / 32) * (height / 32) };
    halide_dimension_t shape[4] = { c_dim, x_dim, y_dim, b_dim };

    halide_dimension_t i_dim{ 0, width * height, 1 };
    halide_dimension_t b_shape[2] = { i_dim };

    // A 6D array of filter coefficients indexed by ci % n, co % k, ci / n, co / k, x, y,

    halide_dimension_t cim_dim{ 0, 4, 1 };
    halide_dimension_t com_dim{ 0, 4, 4 };
    halide_dimension_t cid_dim{ 0, 4, 4 * 4 };
    halide_dimension_t cod_dim{ 0, 4, 4 * 4 * 4 };
    halide_dimension_t fx_dim{ 0, 1, 4 * 4 * 4 * 4 };
    halide_dimension_t fy_dim{ 0, 1, 4 * 4 * 4 * 4 };
    halide_dimension_t f_shape[6] = { cim_dim, com_dim, cid_dim, cod_dim, x_dim, b_dim };

    Halide::Runtime::Buffer<uint8_t> input_buf(input, 4, shape);
    Halide::Runtime::Buffer<uint8_t> output_llvm(output, 4, shape);
    Halide::Runtime::Buffer<uint8_t> output_halide(output, 4, shape);
    Halide::Runtime::Buffer<uint8_t> output_pitchfork(output, 4, shape);
    Halide::Runtime::Buffer<uint8_t> output_rake(output, 4, shape);
    Halide::Runtime::Buffer<uint8_t> filter_buf(input, 6, f_shape);
    Halide::Runtime::Buffer<int32_t> bias_(bias, 1, b_shape);


    // Halide::Runtime::Buffer<uint8_t> input(N, M);
    // Halide::Runtime::Buffer<int8_t> mask(3, 3);

    // Halide::Runtime::Buffer<uint8_t> output_llvm(N, M);
    // Halide::Runtime::Buffer<uint8_t> output_halide(N, M);
    // Halide::Runtime::Buffer<uint8_t> output_pitchfork(N, M);
    // Halide::Runtime::Buffer<uint8_t> output_rake(N, M);

    conv_nn_llvm(input_buf, 3, filter_buf, 5, bias_, 1, 1, 1, 1, 32767, 1, 3, 5, 250, output_llvm);

    double min_t_manual = benchmark(timing_iterations, 10, [&]() {
        conv_nn_llvm(input_buf, 3, filter_buf, 5, bias_, 1, 1, 1, 1, 32767, 1, 3, 5, 250, output_llvm);
        output_llvm.device_sync();
    });
    printf("LLVM time: %gms\n", min_t_manual * 1e3);

    conv_nn_halide(input_buf, 3, filter_buf, 5, bias_, 1, 1, 1, 1, 32767, 1, 3, 5, 250, output_halide);

    min_t_manual = benchmark(timing_iterations, 10, [&]() {
        conv_nn_halide(input_buf, 3, filter_buf, 5, bias_, 1, 1, 1, 1, 32767, 1, 3, 5, 250, output_halide);
        output_halide.device_sync();
    });
    printf("Halide time: %gms\n", min_t_manual * 1e3);

    conv_nn_pitchfork(input_buf, 3, filter_buf, 5, bias_, 1, 1, 1, 1, 32767, 1, 3, 5, 250, output_pitchfork);

    min_t_manual = benchmark(timing_iterations, 10, [&]() {
        conv_nn_pitchfork(input_buf, 3, filter_buf, 5, bias_, 1, 1, 1, 1, 32767, 1, 3, 5, 250, output_pitchfork);
        output_pitchfork.device_sync();
    });
    printf("Pitchfork time: %gms\n", min_t_manual * 1e3);

    conv_nn_rake(input_buf, 3, filter_buf, 5, bias_, 1, 1, 1, 1, 32767, 1, 3, 5, 250, output_rake);

    min_t_manual = benchmark(timing_iterations, 10, [&]() {
        conv_nn_rake(input_buf, 3, filter_buf, 5, bias_, 1, 1, 1, 1, 32767, 1, 3, 5, 250, output_rake);
        output_rake.device_sync();
    });
    printf("Rake time: %gms\n", min_t_manual * 1e3);

    for (int i = 0; i < output_llvm.width(); i++) {
        for (int j = 0; j < output_llvm.height(); j++) {
            for (int k = 0; k < output_llvm.channels(); k++) {
                for (int m = 0; m < output_llvm.channels(); m++) {
                    if (output_llvm(i, j, k, m) != output_halide(i, j, k, m)) {
                        std::cerr << "Halide failure at pixel i=" << i << ", j=" << j << ", k=" << k << ", m=" << m << ": "
                                << (int)output_llvm(i, j, k, m) << " != " << (int)output_halide(i, j, k, m) << "\n";
                        return -1;
                    }

                    if (output_llvm(i, j, k, m) != output_pitchfork(i, j, k, m)) {
                        std::cerr << "Pitchfork failure at pixel i=" << i << ", j=" << j << ", k=" << k << ", m=" << m << ": "
                                << (int)output_llvm(i, j, k, m) << " != " << (int)output_pitchfork(i, j, k, m) << "\n";
                        return -1;
                    }

                    if (output_llvm(i, j, k, m) != output_rake(i, j, k, m)) {
                        std::cerr << "Rake failure at pixel i=" << i << ", j=" << j << ", k=" << k << ", m=" << m << ": "
                                << (int)output_llvm(i, j, k, m) << " != " << (int)output_rake(i, j, k, m) << "\n";
                        return -1;
                    }
                }
            }
        }
    }

    printf("Success!\n");
    return 0;
}
