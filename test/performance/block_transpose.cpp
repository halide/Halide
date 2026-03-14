#include "Halide.h"
#include "halide_benchmark.h"
#include "halide_test_dirs.h"

#include <cstdio>

using namespace Halide;
using namespace Halide::Tools;

enum {
    scalar_trans,
    vec_y_trans,
    vec_x_trans
};

Buffer<uint16_t> test_transpose(int mode) {
    Func input, block, block_transpose, output;
    Var x, y;

    input(x, y) = cast<uint16_t>(x + y);
    input.compute_root();

    block(x, y) = input(x, y);
    block_transpose(x, y) = block(y, x);
    output(x, y) = block_transpose(x, y);

    Var xi, yi;
    output.tile(x, y, xi, yi, 8, 8).vectorize(xi).unroll(yi);

    // Do 8 vectorized loads from the input.
    block.compute_at(output, x).vectorize(x).unroll(y);

    std::string algorithm;
    switch (mode) {
    case scalar_trans:
        block_transpose.compute_at(output, x).unroll(x).unroll(y);
        algorithm = "Scalar transpose";
        output.compile_to_assembly(Internal::get_test_tmp_dir() + "scalar_transpose.s", std::vector<Argument>());
        break;
    case vec_y_trans:
        block_transpose.compute_at(output, x).vectorize(y).unroll(x);
        algorithm = "Transpose vectorized in y";
        output.compile_to_assembly(Internal::get_test_tmp_dir() + "fast_transpose_y.s", std::vector<Argument>());
        break;
    case vec_x_trans:
        block_transpose.compute_at(output, x).vectorize(x).unroll(y);
        algorithm = "Transpose vectorized in x";
        output.compile_to_assembly(Internal::get_test_tmp_dir() + "fast_transpose_x.s", std::vector<Argument>());
        break;
    }

    Buffer<uint16_t> result(1024, 1024);
    output.compile_jit();

    output.realize(result);

    double t = benchmark([&]() {
        output.realize(result);
    });

    std::cout << "Dummy Func version: " << algorithm << " bandwidth " << 1024 * 1024 / t << " byte/s.\n";
    return result;
}

/* This illustrates how to achieve the same scheduling behavior using the 'in()'
 * directive as opposed to creating dummy Funcs as done in 'test_transpose()' */
Buffer<uint16_t> test_transpose_wrap(int mode) {
    Func input, block_transpose, block, output;
    Var x, y;

    input(x, y) = cast<uint16_t>(x + y);
    input.compute_root();

    output(x, y) = input(y, x);

    Var xi, yi;
    output.tile(x, y, xi, yi, 8, 8).vectorize(xi).unroll(yi);

    // Do 8 vectorized loads from the input.
    block_transpose = input.in(output).compute_at(output, x).vectorize(x).unroll(y);

    std::string algorithm;
    switch (mode) {
    case scalar_trans:
        block = block_transpose.in(output).reorder_storage(y, x).compute_at(output, x).unroll(x).unroll(y);
        algorithm = "Scalar transpose";
        output.compile_to_assembly(Internal::get_test_tmp_dir() + "scalar_transpose.s", std::vector<Argument>());
        break;
    case vec_y_trans:
        block = block_transpose.in(output).reorder_storage(y, x).compute_at(output, x).vectorize(y).unroll(x);
        algorithm = "Transpose vectorized in y";
        output.compile_to_assembly(Internal::get_test_tmp_dir() + "fast_transpose_y.s", std::vector<Argument>());
        break;
    case vec_x_trans:
        block = block_transpose.in(output).reorder_storage(y, x).compute_at(output, x).vectorize(x).unroll(y);
        algorithm = "Transpose vectorized in x";
        output.compile_to_assembly(Internal::get_test_tmp_dir() + "fast_transpose_x.s", std::vector<Argument>());
        break;
    }

    Buffer<uint16_t> result(1024, 1024);
    output.compile_jit();

    output.realize(result);

    double t = benchmark([&]() {
        output.realize(result);
    });

    std::cout << "Wrapper version: " << algorithm << " bandwidth " << 1024 * 1024 / t << " byte/s.\n";
    return result;
}

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (target.arch == Target::WebAssembly) {
        printf("[SKIP] Performance tests are meaningless and/or misleading under WebAssembly interpreter.\n");
        return 0;
    }

    test_transpose(scalar_trans);
    test_transpose_wrap(scalar_trans);
    test_transpose(vec_y_trans);
    test_transpose_wrap(vec_y_trans);

    Buffer<uint16_t> im1 = test_transpose(vec_x_trans);
    Buffer<uint16_t> im2 = test_transpose_wrap(vec_x_trans);

    // Check correctness of the wrapper version
    for (int y = 0; y < im2.height(); y++) {
        for (int x = 0; x < im2.width(); x++) {
            if (im2(x, y) != im1(x, y)) {
                printf("wrapper(%d, %d) = %d instead of %d\n",
                       x, y, im2(x, y), im1(x, y));
                return 1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
