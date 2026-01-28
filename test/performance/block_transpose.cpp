#include "Halide.h"
#include "halide_benchmark.h"
#include "halide_test_dirs.h"

#include <cstdio>

using namespace Halide;
using namespace Halide::Tools;

struct Result {
    int type_size, block_width, block_height;
    double bandwidth;
};

template<typename T>
Result test_transpose(int block_width, int block_height, const Target &t) {
    const int N = 256;
    Buffer<T> in(N, N), out(N, N);

    for (int y = 0; y < N; y++) {
        for (int x = 0; x < N; x++) {
            in(x, y) = (T)(x + y * N);
        }
    }

    Func input, block_transpose, block, output;
    Var x, y;

    input(x, y) = in(x, y);

    output(x, y) = input(y, x);

    Var xi, yi;
    output.tile(x, y, xi, yi, block_width, block_height, TailStrategy::RoundUp)
        .vectorize(xi)
        .unroll(yi);

    // Do vectorized loads from the input.
    input.in().compute_at(output, x).vectorize(x).unroll(y);

    // Transpose in registers
    input.in().in().reorder_storage(y, x).compute_at(output, x).vectorize(x).unroll(y);

    // TODO: Should not be necessary, but prevents licm from doing something dumb.
    output.output_buffer().dim(0).set_bounds(0, 256);

    output.compile_jit();

    output.realize(out);

    double time = benchmark(10, 10, [&]() {
        output.realize(out);
    });

    for (int y = 0; y < N; y++) {
        for (int x = 0; x < N; x++) {
            T actual = out(x, y), correct = in(y, x);
            if (actual != correct) {
                std::cerr << "For block size (" << block_width << ", " << block_height << "): "
                          << "out(" << x << ", " << y << ") = "
                          << actual << " instead of " << correct << "\n";
                exit(1);
            }
        }
    }

    // Uncomment to dump asm for inspection
    /*
    output.compile_to_assembly(Internal::get_test_tmp_dir() + "transpose_uint" +
                                   std::to_string(sizeof(T) * 8) + "_" +
                                   std::to_string(block_width) + "x" +
                                   std::to_string(block_height) + ".s",
                               std::vector<Argument>{in}, "transpose", t);
    */

    return Result{(int)sizeof(T), block_width, block_height,
                  out.size_in_bytes() / (1.0e9 * time)};
}

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (target.arch == Target::WebAssembly) {
        printf("[SKIP] Performance tests are meaningless and/or misleading under WebAssembly interpreter.\n");
        return 0;
    }

    // Set the target features to use for dumping to assembly
    target.set_features({Target::NoRuntime, Target::NoAsserts, Target::NoBoundsQuery});

    std::cout << "Computing best tile sizes for each type\n";
    std::vector<Result> results;
    int limit = 64 * 64;
    for (int bh : {1, 2, 4, 8, 16, 32, 64}) {
        for (int bw : {1, 2, 4, 8, 16, 32, 64}) {
            std::cout << "." << std::flush;
            results.push_back(test_transpose<uint8_t>(bw, bh, target));
            if (bw * bh <= limit / 2) {
                results.push_back(test_transpose<uint16_t>(bw, bh, target));
            }
            if (bw * bh <= limit / 4) {
                results.push_back(test_transpose<uint32_t>(bw, bh, target));
            }
            if (bw * bh <= limit / 8) {
                results.push_back(test_transpose<uint64_t>(bw, bh, target));
            }
        }
    }
    std::cout << "\nbytes, tile width, tile height, bandwidth (GB/s):\n";

    // Sort the results by bandwidth
    std::sort(results.begin(), results.end(),
              [](const Result &a, const Result &b) {
                  return a.bandwidth > b.bandwidth;
              });

    // Print top n tile sizes for each type
    for (int t : {1, 2, 4, 8}) {
        int top_n = 5;
        for (size_t i = 0; i < results.size() && top_n > 0; i++) {
            if (results[i].type_size == t) {
                std::cout << t << " "
                          << results[i].block_width << " "
                          << results[i].block_height << " "
                          << results[i].bandwidth << "\n";
                top_n--;
            }
        }
        std::cout << "\n";
    }

    printf("Success!\n");
    return 0;
}
