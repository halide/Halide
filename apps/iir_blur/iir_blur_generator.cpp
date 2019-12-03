// This file defines a generator for a first order IIR low pass filter
// for a 2D image.

#include "Halide.h"

using namespace Halide;
using namespace Halide::BoundaryConditions;

Var x, y, c;

// Defines a func to blur the columns of an input with a first order low
// pass IIR filter, followed by a transpose.
Func blur_cols_transpose(Func input, Expr height, Expr alpha, bool skip_schedule, Target target) {
    Func blur("blur");

    const int vec = target.natural_vector_size<float>();

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
    Func transpose("transpose");
    transpose(x, y, c) = blur(y, x, c);

    // Schedule
    if (!skip_schedule) {
        if (!target.has_gpu_feature()) {
            // CPU schedule.
            // 8.2ms on an Intel i9-9960X using 16 threads
            // Split the transpose into tiles of rows. Parallelize over channels
            // and strips (Halide supports nested parallelism).
            Var xo, yo, t;
            transpose.compute_root()
                .tile(x, y, xo, yo, x, y, vec, vec*4)
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
        } else {
            // GPU schedule

            // Really for an IIR on the GPU you should use a more
            // specialized DSL like RecFilter, but we can schedule it
            // in Halide adequately, we just can't extract any
            // parallelism from the scan dimension.

            // We also have no way to store kernel intermediates on
            // the GPU in global memory, and we can't fuse together
            // multiple update stages into a single loop, so we
            // currently have to launch a whole lotta
            // kernels. store_with would ameliorate this to some
            // degree, or making store_in get you to global memory on
            // the GPU. All in all, this is a pretty mediocre schedule
            // for an IIR blur.

            // 3.39ms on a 2060 RTX

            Var xi, yi;
            blur.compute_root();
            blur.update(0)
                .split(x, x, xi, 32)
                .gpu_blocks(x, c)
                .gpu_threads(xi);
            blur.update(1)
                .split(x, x, xi, 32)
                .gpu_blocks(x, c)
                .gpu_threads(xi);
            blur.update(2)
                .split(x, x, xi, 32)
                .gpu_blocks(x, c)
                .gpu_threads(xi);
        }

    }

    return transpose;
}

class IirBlur : public Generator<IirBlur> {
public:
    // This is the input image: a 3D (color) image with 32 bit float
    // pixels.
    Input<Buffer<float>> input{"input", 3};
    // The filter coefficient, alpha is the weight of the input to the
    // filter.
    Input<float> alpha{"alpha"};

    Output<Buffer<float>> output{"output", 3};

    void generate() {
        Expr width = input.width();
        Expr height = input.height();

        // First, blur the columns of the input.
        Func blury_T = blur_cols_transpose(input, height, alpha, auto_schedule, get_target());

        // Blur the columns again (the rows of the original).
        Func blur = blur_cols_transpose(blury_T, width, alpha, auto_schedule, get_target());

        // Scheduling is done inside blur_cols_transpose.
        output = blur;

        if (get_target().has_gpu_feature() && !auto_schedule) {
            Var xi, yi;
            output.compute_root()
                .reorder(c, x, y)
                .gpu_tile(x, y, xi, yi, 32, 32);
        }

        // Estimates
        {
            input.dim(0).set_estimate(0, 1536)
                   .dim(1).set_estimate(0, 2560)
                   .dim(2).set_estimate(0, 3);
            alpha.set_estimate(0.1f);
            output.dim(0).set_estimate(0, 1536)
                   .dim(1).set_estimate(0, 2560)
                   .dim(2).set_estimate(0, 3);
        }
    }
};

HALIDE_REGISTER_GENERATOR(IirBlur, iir_blur)
