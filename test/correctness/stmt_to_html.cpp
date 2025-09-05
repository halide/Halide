#include "Halide.h"
#include "halide_test_dirs.h"
#include <gtest/gtest.h>

using namespace Halide;

namespace {
Var x, y;
}

TEST(StmtToHtmlTest, GradientFast) {
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

    std::string result_file = Internal::get_test_tmp_dir() + "stmt_to_html_dump_1.html";
    Internal::ensure_no_file_exists(result_file);
    ASSERT_NO_THROW(gradient_fast.compile_to_lowered_stmt(result_file, {}, Halide::HTML));
    Internal::assert_file_exists(result_file);
}

TEST(StmtToHtmlTest, WithImage) {
    Func gradient_fast("gradient_fast");
    gradient_fast(x, y) = x + y;

    std::string result_file = Internal::get_test_tmp_dir() + "stmt_to_html_dump_2.html";
    Internal::ensure_no_file_exists(result_file);
    Buffer<int> im(800, 600);
    ASSERT_NO_THROW(gradient_fast.compile_to_lowered_stmt(result_file, {im}, Halide::HTML));
    Internal::assert_file_exists(result_file);
}

TEST(StmtToHtmlTest, MultiOutput) {
    std::string result_file = Internal::get_test_tmp_dir() + "stmt_to_html_dump_3.html";
    Internal::ensure_no_file_exists(result_file);
    Func tuple_func;
    tuple_func(x, y) = Tuple(x, y);
    ASSERT_NO_THROW(tuple_func.compile_to_lowered_stmt(result_file, {}, Halide::HTML));
    Internal::assert_file_exists(result_file);
}
