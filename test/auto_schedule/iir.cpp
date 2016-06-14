#include "Halide.h"
using namespace Halide;

Var x("x"), y("y"), c("c");

Func blur_cols_transpose(Func input, Expr height, Expr alpha) {
    Func blur("blur_in");

    // Pure definition: do nothing.
    blur(x, y, c) = undef<float>();
    // Update 0: set the top row of the result to the input.
    blur(x, 0, c) = input(x, 0, c);
    // Update 1: run the IIR filter down the columns.
    RDom ry(1, height - 1);
    blur(x, ry, c) =
        (1 - alpha)*blur(x, ry - 1, c) + alpha*input(x, ry, c);
    // Update 2: run the IIR blur up the columns.
    Expr flip_ry = height - ry - 1;
    blur(x, flip_ry, c) =
        (1 - alpha)*blur(x, flip_ry + 1, c) + alpha*blur(x, flip_ry, c);

    // Transpose the blur.
    Func transpose;
    transpose(x, y, c) = blur(y, x, c);

    return transpose;
}

int main(int argc, char **argv) {

    int H = 1024;
    int W = 2048;
    Image<float> input(H, W, 3);

    for (int c = 0; c < 3; c++) {
        for (int y = 0; y < input.height(); y++) {
            for (int x = 0; x < input.width(); x++) {
                input(x, y, c) = rand() & 0xfff;
            }
        }
    }

    Expr alpha = 0.1f;

    Expr width = input.width();
    Expr height = input.height();

    // Our input is an ImageParam, but blur_cols takes a Func, so
    // we define a trivial func to wrap the input.
    Func input_func;
    input_func(x, y, c) = input(x, y, c);

    // First, blur the columns of the input.
    Func blury_T = blur_cols_transpose(input_func, height, alpha);

    // Blur the columns again (the rows of the original).
    Func blur = blur_cols_transpose(blury_T, width, alpha);


    // Specifying estimates
    blur.estimate(x, 0, 1024).estimate(y, 0, 2048).estimate(c, 0, 3);

    // Auto schedule the pipeline
    Target target = get_target_from_environment();
    Pipeline p(blur);

    p.auto_schedule(target);

    // Inspect the schedule
    blur.print_loop_nest();

    // Run the schedule
    Image<float> out = p.realize(1024, 2048, 3);

    printf("Success!\n");
    return 0;
}
