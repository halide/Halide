// Based of Halide tutorial 5

#include <Halide.h>
#include <stdio.h>
using namespace Halide;

int main() {

      Var x, y;

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

      
      gradient_fast.compile_to_simplified_lowered_stmt("stmt_to_html_dump.html", 800, 600, Halide::HTML);

    printf("Success!\n");
    return 0;
}
