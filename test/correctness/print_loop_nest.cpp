#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    {
        Func output_y, output_u, output_v;
        Var x, y, x_outer, y_outer, x_inner, y_inner;
        Buffer<int> input(960, 960, 3);
        output_y(x, y) = input(x, y, 0);
        output_u(x, y) = input(2 * x, 2 * y, 1);
        output_v(x, y) = input(2 * x, 2 * y, 2);
        output_u.tile(x, y, x_outer, y_outer, x_inner, y_inner, 4, 1);
        output_v.tile(x, y, x_outer, y_outer, x_inner, y_inner, 4, 1);
        output_y.tile(x, y, x_outer, y_outer, x_inner, y_inner, 4 * 2, 2);

        output_u.compute_with(output_y, x_outer);
        output_v.compute_with(output_y, x_outer);

        Pipeline p({output_y, output_u, output_v});
        p.print_loop_nest();
    }
    printf("Success!\n");
    return 0;
}
