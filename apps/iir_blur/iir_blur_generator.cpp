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
        (1 - alpha) * blur(x, ry - 1, c) + alpha * input(x, ry, c);
    // Update 2: run the IIR blur up the columns.
    Expr flip_ry = height - ry - 1;
    blur(x, flip_ry, c) =
        (1 - alpha) * blur(x, flip_ry + 1, c) + alpha * blur(x, flip_ry, c);

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
                .tile(x, y, xo, yo, x, y, vec, vec * 4)
                .vectorize(x)
                .parallel(yo)
                .parallel(c);

            // Run the filter on each row of tiles (which corresponds to a strip of
            // columns in the input).
            blur.compute_at(transpose, yo);

            // Vectorize computations within the strips.
            blur.update(0)
                .unscheduled();
            blur.update(1)
                .reorder(x, ry)
                .vectorize(x);
            blur.update(2)
                .reorder(x, ry)
                .vectorize(x);
        } else if (target.has_feature(Target::CUDA)) {
            // CUDA-specific GPU schedule (using gpu_lanes)

            // Really for an IIR on the GPU you should use a more
            // specialized DSL like RecFilter, but we can schedule it
            // in Halide adequately, we just can't extract any
            // parallelism from the scan dimension. Most GPUs will be
            // heavily under-utilized with this schedule and thus
            // unable to hide the memory latencies to L2.

            const int warp_size = 32;

            // 2.06ms on a 2060 RTX
            Var xi, yi;
            transpose.compute_root()
                .tile(x, y, xi, yi, warp_size, warp_size)
                .gpu_blocks(y, c)
                .gpu_lanes(xi);

            blur.compute_at(transpose, y)
                .store_in(MemoryType::Heap)  // Too large to fit into shared memory
                .gpu_lanes(x);
            blur.update(0)
                .gpu_lanes(x);

            // We can't hide load latencies by swapping in other warps
            // because we don't have enough available parallelism for
            // that, but if we unroll the scan loop a little then the
            // ptx compiler can reorder the loads earlier than the
            // fmas, and cover latency that way. Saves 1.7ms!
            blur.update(1)
                .unroll(ry, 8)
                .gpu_lanes(x);
            blur.update(2)
                .unroll(ry, 8)
                .gpu_lanes(x);

            // Stage the transpose input through shared so that we do
            // strided loads out of shared instead of global.  By
            // default the stride would be the width of the
            // allocation, which is the warp size. This can cause bank
            // conflicts. We can improve matters by padding out the
            // storage horizontally to make the stride coprime with
            // the warp size, so that each load has a distinct
            // remainder modulo the warp size. warp_size + 1 will
            // do. This saves 0.05 ms
            blur.in()
                .align_storage(x, warp_size + 1)
                .compute_at(transpose, x)
                .gpu_lanes(x);
        } else {
            // Generic GPU schedule (for gpus without gpu_lanes() support)
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
    Input<Buffer<float, 3>> input{"input"};
    // The filter coefficient, alpha is the weight of the input to the
    // filter.
    Input<float> alpha{"alpha"};

    Output<Buffer<float, 3>> output{"output"};

    void generate() {
        Expr width = input.width();
        Expr height = input.height();

        // First, blur the columns of the input.
        Func blury_T = blur_cols_transpose(input, height, alpha, using_autoscheduler(), get_target());

        // Blur the columns again (the rows of the original).
        Func blur = blur_cols_transpose(blury_T, width, alpha, using_autoscheduler(), get_target());

        // Scheduling is done inside blur_cols_transpose.
        output = blur;

        // Estimates
        {
            input.dim(0).set_estimate(0, 1536);
            input.dim(1).set_estimate(0, 2560);
            input.dim(2).set_estimate(0, 3);
            alpha.set_estimate(0.1f);
            output.dim(0).set_estimate(0, 1536);
            output.dim(1).set_estimate(0, 2560);
            output.dim(2).set_estimate(0, 3);
        }
    }
};

HALIDE_REGISTER_GENERATOR(IirBlur, iir_blur)
