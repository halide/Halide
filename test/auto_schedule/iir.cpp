#include "Halide.h"
#include "halide_benchmark.h"

using namespace Halide;
using namespace Halide::Tools;

Var x("x"), y("y"), c("c");

Func blur_cols_transpose(Func input, Expr height, Expr alpha,
                         bool auto_schedule) {
    Func blur("blur_in");

    // Pure definition: do nothing.
    blur(x, y, c) = undef<float>();
    // Update 0: set the top row of the result to the input.
    blur(x, 0, c) = input(x, 0, c);
    // Update 1: run the IIR filter down the columns.
    RDom ry(1, height - 1);
    blur(x, ry, c) = (1 - alpha)*blur(x, ry - 1, c) + alpha*input(x, ry, c);
    // Update 2: run the IIR blur up the columns.
    Expr flip_ry = height - ry - 1;
    blur(x, flip_ry, c) = (1 - alpha)*blur(x, flip_ry + 1, c) + alpha*blur(x, flip_ry, c);

    // Transpose the blur.
    Func transpose;
    transpose(x, y, c) = blur(y, x, c);

    if (!auto_schedule) {
        // Schedule:
        // Split the transpose into tiles of rows. Parallelize over channels
        // and strips (Halide supports nested parallelism).
        Var xo, yo;
        transpose.compute_root()
            .tile(x, y, xo, yo, x, y, 8, 8)
            .vectorize(x)
            .parallel(yo)
            .parallel(c);

        // Run the filter on each row of tiles (which corresponds to a strip of
        // columns in the input).
        blur.compute_at(transpose, yo);

        // Vectorize computations within the strips.
        blur.update(1)
            .reorder(x, ry)
            .vectorize(x);
        blur.update(2)
            .reorder(x, ry)
            .vectorize(x);
    }

    return transpose;
}

double run_test(bool auto_schedule) {

    int W = 2048;
    int H = 2048;
    Buffer<float> input(W, H, 3);

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
    Func input_func("input_func");
    input_func(x, y, c) = input(x, y, c);

    // First, blur the columns of the input.
    Func blury_T = blur_cols_transpose(input_func, height, alpha, auto_schedule);

    // Blur the columns again (the rows of the original).
    Func blur = blur_cols_transpose(blury_T, width, alpha, auto_schedule);

    Target target = get_jit_target_from_environment();
    Pipeline p(blur);

    if (auto_schedule) {
        // Provide estimates on the pipeline output
        blur.estimate(x, 0, W)
            .estimate(y, 0, H)
            .estimate(c, 0, 3);
        // Auto-schedule the pipeline
        p.auto_schedule(target);
    }

    // Inspect the schedule
    blur.print_loop_nest();

    // Run the schedule
    Buffer<float> out(W, H, 3);
    double t = benchmark(3, 10, [&]() {
        p.realize(out);
    });

    return t*1000;
}

int main(int argc, char **argv) {
    double manual_time = run_test(false);
    double auto_time = run_test(true);

    std::cout << "======================" << std::endl;
    std::cout << "Manual time: " << manual_time << "ms" << std::endl;
    std::cout << "Auto time: " << auto_time << "ms" << std::endl;
    std::cout << "======================" << std::endl;

    if (auto_time > manual_time * 5) {
        printf("Auto-scheduler is much much slower than it should be.\n");
        return -1;
    }

    printf("Success!\n");
    return 0;
}
