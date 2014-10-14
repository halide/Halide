#include <Halide.h>
#include <stdio.h>
#include "clock.h"
#include <memory>

using namespace Halide;

enum {
    scalar_trans,
    vec_y_trans,
    vec_x_trans
};

void test_transpose(int mode) {
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
    switch(mode) {
        case scalar_trans:
            block_transpose.compute_at(output, x).unroll(x).unroll(y);
            algorithm = "Scalar transpose";
            break;
        case vec_y_trans:
            block_transpose.compute_at(output, x).vectorize(y).unroll(x);
            algorithm = "Transpose vectorized in y";
            break;
        case vec_x_trans:
            block_transpose.compute_at(output, x).vectorize(x).unroll(y);
            algorithm = "Transpose vectorized in x";
            break;
    }

    output.compile_to_lowered_stmt("fast_transpose.stmt");
    output.compile_to_assembly("fast_transpose.s", std::vector<Argument>());

    Image<uint16_t> result(1024, 1024);
    output.compile_jit();

    output.realize(result);

    double t1 = current_time();
    for (int i = 0; i < 10; i++) {
        output.realize(result);
    }
    double t2 = current_time();

    std::cout << algorithm << " bandwidth " << ((1024*1024 / (t2 - t1)) * 1000 * 10) << " byte/s.\n";
}

int main(int argc, char **argv) {
    test_transpose(scalar_trans);
    test_transpose(vec_y_trans);
    test_transpose(vec_x_trans);
    printf("Success!\n");
    return 0;
}
