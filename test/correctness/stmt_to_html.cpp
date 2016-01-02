#include "Halide.h"
#include <stdio.h>
#ifndef _MSC_VER
#include <unistd.h>
#endif

using namespace Halide;

int main() {
    Var x, y;

    // The gradient function and schedule from tutorial lesson 5.
    Func gradient_fast("gradient_fast");
    gradient_fast(x, y) = x + y;

    Var x_outer, y_outer, x_inner, y_inner, tile_index;
    gradient_fast
        .tile(x, y, x_outer, y_outer, x_inner, y_inner, 256, 256)
        .fuse(x_outer, y_outer, tile_index)
        .parallel(tile_index);

    Var x_inner_outer, y_inner_outer, x_vectors, y_pairs;
    gradient_fast
        .tile(x_inner, y_inner, x_inner_outer, y_inner_outer, x_vectors, y_pairs, 4, 2)
        .vectorize(x_vectors)
        .unroll(y_pairs);


    const char *result_file_1 = "stmt_to_html_dump_1.html";
    gradient_fast.compile_to_lowered_stmt(result_file_1, {}, Halide::HTML);

    #ifndef _MSC_VER
    assert(access(result_file_1, F_OK) == 0 && "Output file not created.");
    #endif

    // Also check using an image.
    const char *result_file_2 = "stmt_to_html_dump_2.html";
    Image<int> im(800, 600);
    gradient_fast.compile_to_lowered_stmt(result_file_2, {im}, Halide::HTML);

    #ifndef _MSC_VER
    assert(access(result_file_2, F_OK) == 0 && "Output file not created.");
    #endif

    // Check a multi-output pipeline.
    const char *result_file_3 = "stmt_to_html_dump_3.html";
    Func tuple_func;
    tuple_func(x, y) = Tuple(x, y);
    tuple_func.compile_to_lowered_stmt(result_file_3, {}, Halide::HTML);

    #ifndef _MSC_VER
    assert(access(result_file_2, F_OK) == 0 && "Output file not created.");
    #endif

    printf("Success!\n");
    return 0;
}
