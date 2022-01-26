// This file defines a generator for a first order IIR low pass filter
// for a 2D image.

#include "Halide.h"

using namespace Halide;
using namespace Halide::BoundaryConditions;

Var x, y, c;

// Defines a func to blur the columns of an input with a first order low
// pass IIR filter, followed by a transpose.
Func blur_cols_transpose(Func input, Expr height, Expr alpha) {
    Func blur;

    // Pure definition: do nothing.
    blur(x, y, c) = undef<float>();
    // Update 0: set the top row of the result to the input.
    blur(x, 0, c) = input(x, 0, c);
    // Update 1: run the IIR filter down the columns.
    RDom ry(1, height - 1);
    blur(x, ry, c) =
        (1 - alpha) * blur(x, ry - 1, c) + alpha * input(x, ry, c);
    // Update 2: run the IIR blur up the columns.
    Expr flip_ry = height - ry - 1;
    blur(x, flip_ry, c) =
        (1 - alpha) * blur(x, flip_ry + 1, c) + alpha * blur(x, flip_ry, c);

    // Transpose the blur.
    Func transpose;
    transpose(x, y, c) = blur(y, x, c);

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

    return transpose;
}

class IirBlur : public Generator<IirBlur> {
public:
    // This is the input image: a 3D (color) image with 32 bit float
    // pixels.
    Input<Buffer<float, 3>> input{"input"};
    // The filter coefficient, alpha is the weight of the input to the
    // filter.
    Input<float> alpha{"alpha"};

    Output<Buffer<float, 3>> output{"output"};

    void generate() {
        Expr width = input.width();
        Expr height = input.height();

        // First, blur the columns of the input.
        Func blury_T = blur_cols_transpose(input, height, alpha);

        // Blur the columns again (the rows of the original).
        Func blur = blur_cols_transpose(blury_T, width, alpha);

        // Scheduling is done inside blur_cols_transpose.
        output(x, y, c) = blur(x, y, c);
    }
};

HALIDE_REGISTER_GENERATOR(IirBlur, IirBlur)
