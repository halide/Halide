#include "Halide.h"
#include "halide_test_dirs.h"

#include <cstdio>

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

    std::string result_file_1 = Internal::get_test_tmp_dir() + "stmt_to_viz_dump_1.stmt.viz.html";
    Internal::ensure_no_file_exists(result_file_1);
    gradient_fast.compile_to_lowered_stmt(result_file_1, {}, Halide::StmtViz);
    Internal::assert_file_exists(result_file_1);

    // Also check using an image.
    std::string result_file_2 = Internal::get_test_tmp_dir() + "stmt_to_html_dump_2.stmt.viz.html";
    Internal::ensure_no_file_exists(result_file_2);
    Buffer<int> im(800, 600);
    gradient_fast.compile_to_lowered_stmt(result_file_2, {im}, Halide::StmtViz);
    Internal::assert_file_exists(result_file_2);

    // Check a multi-output pipeline.
    std::string result_file_3 = Internal::get_test_tmp_dir() + "stmt_to_html_dump_3.stmt.viz.html";
    Internal::ensure_no_file_exists(result_file_3);
    Func tuple_func;
    tuple_func(x, y) = Tuple(x, y);
    tuple_func.compile_to_lowered_stmt(result_file_3, {}, Halide::StmtViz);
    Internal::assert_file_exists(result_file_3);

    printf("Success!\n");
    return 0;
}
