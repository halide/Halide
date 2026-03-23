#include "Halide.h"

using namespace Halide;

enum ScheduleType {
    Naive,
    LessNaive,
    Complex
};

// This is a dumbed-down version of the Resize generator, intended solely
// to demonstrate use of HalideTraceViz auto-layout mode; it has multiple
// schedule, ranging from 'naive' to 'complex', intended to demonstrate
// how even basic auto-layout of tracing can be useful.
//
// The approach of using an enum for 'naive-of-complex-schedule' is an expedient
// for this purpose; it shouldn't me mimicked in most real-world code.
class AutoVizDemo : public Halide::Generator<AutoVizDemo> {
public:
    GeneratorParam<ScheduleType> schedule_type{"schedule_type", Naive, {{"naive", Naive}, {"lessnaive", LessNaive}, {"complex", Complex}}};

    // If we statically know whether we're upsampling or downsampling,
    // we can generate different pipelines (we want to reorder the
    // resample in x and in y).
    GeneratorParam<bool> upsample{"upsample", false};

    Input<Buffer<float, 3>> input{"input"};
    Input<float> scale_factor{"scale_factor"};
    Output<Buffer<float, 3>> output{"output"};

    // Common Vars
    Var x, y, c, k;

    // Intermediate Funcs
    Func as_float, clamped, resized_x, resized_y,
        unnormalized_kernel_x, unnormalized_kernel_y,
        kernel_x, kernel_y,
        kernel_sum_x, kernel_sum_y;

    void generate() {

        clamped = BoundaryConditions::repeat_edge(input,
                                                  {{input.dim(0).min(), input.dim(0).extent()},
                                                   {input.dim(1).min(), input.dim(1).extent()}});

        // Handle different types by just casting to float
        as_float(x, y, c) = cast<float>(clamped(x, y, c));

        // For downscaling, widen the interpolation kernel to perform lowpass
        // filtering.

        Expr kernel_scaling = upsample ? Expr(1.0f) : scale_factor;

        Expr kernel_radius = 0.5f / kernel_scaling;

        Expr kernel_taps = ceil(1.0f / kernel_scaling);

        // source[xy] are the (non-integer) coordinates inside the source image
        Expr sourcex = (x + 0.5f) / scale_factor - 0.5f;
        Expr sourcey = (y + 0.5f) / scale_factor - 0.5f;

        // Initialize interpolation kernels. Since we allow an arbitrary
        // scaling factor, the filter coefficients are different for each x
        // and y coordinate.
        Expr beginx = cast<int>(ceil(sourcex - kernel_radius));
        Expr beginy = cast<int>(ceil(sourcey - kernel_radius));

        RDom r(0, kernel_taps);

        auto kernel = [](Expr x) -> Expr {
            Expr xx = abs(x);
            return select(abs(x) <= 0.5f, 1.0f, 0.0f);
        };
        unnormalized_kernel_x(x, k) = kernel((k + beginx - sourcex) * kernel_scaling);
        unnormalized_kernel_y(y, k) = kernel((k + beginy - sourcey) * kernel_scaling);

        kernel_sum_x(x) = sum(unnormalized_kernel_x(x, r), "kernel_sum_x");
        kernel_sum_y(y) = sum(unnormalized_kernel_y(y, r), "kernel_sum_y");

        kernel_x(x, k) = unnormalized_kernel_x(x, k) / kernel_sum_x(x);
        kernel_y(y, k) = unnormalized_kernel_y(y, k) / kernel_sum_y(y);

        // Perform separable resizing. The resize in x vectorizes
        // poorly compared to the resize in y, so do it first if we're
        // upsampling, and do it second if we're downsampling.
        Func resized;
        if (upsample) {
            resized_x(x, y, c) = sum(kernel_x(x, r) * as_float(r + beginx, y, c), "resized_x");
            resized_y(x, y, c) = sum(kernel_y(y, r) * resized_x(x, r + beginy, c), "resized_y");
            resized = resized_y;
        } else {
            resized_y(x, y, c) = sum(kernel_y(y, r) * as_float(x, r + beginy, c), "resized_y");
            resized_x(x, y, c) = sum(kernel_x(x, r) * resized_y(r + beginx, y, c), "resized_x");
            resized = resized_x;
        }

        if (input.type().is_float()) {
            output(x, y, c) = clamp(resized(x, y, c), 0.0f, 1.0f);
        } else {
            output(x, y, c) = saturating_cast(input.type(), resized(x, y, c));
        }
    }

    void schedule() {
        Var xi, yi;
        if (schedule_type == Naive) {
            // naive: compute_root() everything
            unnormalized_kernel_x
                .compute_root();
            kernel_sum_x
                .compute_root();
            kernel_x
                .compute_root();
            unnormalized_kernel_y
                .compute_root();
            kernel_sum_y
                .compute_root();
            kernel_y
                .compute_root();
            as_float
                .compute_root();
            resized_x
                .compute_root();
            output
                .compute_root();
        } else if (schedule_type == LessNaive) {
            // less-naive: add vectorization and parallelism to 'large' realizations;
            // use compute_at for as_float calculation
            unnormalized_kernel_x
                .compute_root();
            kernel_sum_x
                .compute_root();
            kernel_x
                .compute_root();

            unnormalized_kernel_y
                .compute_root();
            kernel_sum_y
                .compute_root();
            kernel_y
                .compute_root();

            as_float
                .compute_at(resized_x, y);
            resized_x
                .compute_root()
                .parallel(y);
            output
                .compute_root()
                .parallel(y)
                .vectorize(x, 8);
        } else if (schedule_type == Complex) {
            // complex: use compute_at() and tiling intelligently.
            unnormalized_kernel_x
                .compute_at(kernel_x, x)
                .vectorize(x);
            kernel_sum_x
                .compute_at(kernel_x, x)
                .vectorize(x);
            kernel_x
                .compute_root()
                .reorder(k, x)
                .vectorize(x, 8);

            unnormalized_kernel_y
                .compute_at(kernel_y, y)
                .vectorize(y, 8);
            kernel_sum_y
                .compute_at(kernel_y, y)
                .vectorize(y);
            kernel_y
                .compute_at(output, y)
                .reorder(k, y)
                .vectorize(y, 8);

            if (upsample) {
                as_float
                    .compute_at(output, y)
                    .vectorize(x, 8);
                resized_x
                    .compute_at(output, x)
                    .vectorize(x, 8);
                output
                    .tile(x, y, xi, yi, 16, 64)
                    .parallel(y)
                    .vectorize(xi);
            } else {
                resized_y
                    .compute_at(output, y)
                    .vectorize(x, 8);
                resized_x
                    .compute_at(output, xi);
                output
                    .tile(x, y, xi, yi, 32, 8)
                    .parallel(y)
                    .vectorize(xi);
            }
        }
    }
};

HALIDE_REGISTER_GENERATOR(AutoVizDemo, auto_viz_demo);
