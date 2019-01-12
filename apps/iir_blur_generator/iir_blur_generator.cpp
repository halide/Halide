// This file defines a generator for a first order IIR low pass filter
// for a 2D image.

#include "Halide.h"
#include "../autoscheduler/SimpleAutoSchedule.h"

using namespace Halide;
using namespace Halide::BoundaryConditions;

Var x, y, c;

// Defines a func to blur the columns of an input with a first order low
// pass IIR filter, followed by a transpose.
Func blur_cols_transpose(Func input, Expr height, Expr alpha, bool skip_schedule) {
    Func blur;

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

    // Schedule
    if (!skip_schedule) {
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

        std::string use_simple_autoscheduler =
            Halide::Internal::get_env_variable("HL_USE_SIMPLE_AUTOSCHEDULER");
        bool skip_schedule = use_simple_autoscheduler == "1" || auto_schedule;

        // First, blur the columns of the input.
        Func blury_T = blur_cols_transpose(input, height, alpha, skip_schedule);

        // Blur the columns again (the rows of the original).
        Func blur = blur_cols_transpose(blury_T, width, alpha, skip_schedule);

        // Scheduling is done inside blur_cols_transpose.
        output(x, y, c) = blur(x, y, c);

        // Estimates
        if (use_simple_autoscheduler == "1") {
            Halide::SimpleAutoscheduleOptions options;
            options.gpu = get_target().has_gpu_feature();
            options.gpu_tile_channel = 1;
            Func output_func = output;
            Halide::simple_autoschedule(output_func,
                    {{"alpha", 0.1f},
                     {"input.min.0", 0},
                     {"input.extent.0", 1536},
                     {"input.min.1", 0},
                     {"input.extent.1", 2560},
                     {"input.min.2", 0},
                     {"input.extent.2", 3}},
                    {{0, 1536},
                     {0, 2560},
                     {0, 3}},
                    options);
        }
        {
            input.dim(0).set_bounds_estimate(0, 1536)
                   .dim(1).set_bounds_estimate(0, 2560)
                   .dim(2).set_bounds_estimate(0, 3);
            alpha.set_estimate(0.1f);
            output.dim(0).set_bounds_estimate(0, 1536)
                   .dim(1).set_bounds_estimate(0, 2560)
                   .dim(2).set_bounds_estimate(0, 3);
        }
    }
};

HALIDE_REGISTER_GENERATOR(IirBlur, iir_blur)
