#include "Halide.h"

using namespace Halide;

enum InterpolationType {
    Box,
    Linear,
    Cubic,
    Lanczos
};

Expr kernel_box(Expr x) {
    Expr xx = abs(x);
    return select(xx <= 0.5f, 1.0f, 0.0f);
}

Expr kernel_linear(Expr x) {
    Expr xx = abs(x);
    return select(xx < 1.0f, 1.0f - xx, 0.0f);
}

Expr kernel_cubic(Expr x) {
    Expr xx = abs(x);
    Expr xx2 = xx * xx;
    Expr xx3 = xx2 * xx;
    float a = -0.5f;

    return select(xx < 1.0f, (a + 2.0f) * xx3 - (a + 3.0f) * xx2 + 1,
                  select(xx < 2.0f, a * xx3 - 5 * a * xx2 + 8 * a * xx - 4.0f * a,
                         0.0f));
}

Expr sinc(Expr x) {
    x *= 3.14159265359f;
    return sin(x) / x;
}

Expr kernel_lanczos(Expr x) {
    Expr value = sinc(x) * sinc(x / 3);
    value = select(x == 0.0f, 1.0f, value);        // Take care of singularity at zero
    value = select(x > 3 || x < -3, 0.0f, value);  // Clamp to zero out of bounds
    return value;
}

struct KernelInfo {
    const char *name;
    int taps;
    Expr (*kernel)(Expr);
};

static KernelInfo kernel_info[] = {
    {"box", 1, kernel_box},
    {"linear", 2, kernel_linear},
    {"cubic", 4, kernel_cubic},
    {"lanczos", 6, kernel_lanczos}};

class Resize : public Halide::Generator<Resize> {
public:
    GeneratorParam<InterpolationType> interpolation_type{"interpolation_type", Cubic, {{"box", Box}, {"linear", Linear}, {"cubic", Cubic}, {"lanczos", Lanczos}}};

    // If we statically know whether we're upsampling or downsampling,
    // we can generate different pipelines (we want to reorder the
    // resample in x and in y).
    GeneratorParam<bool> upsample{"upsample", false};

    Input<Buffer<void, 3>> input{"input"};
    Input<float> scale_factor{"scale_factor"};
    Output<Buffer<void, 3>> output{"output"};

    // Common Vars
    Var x, y, c, k;

    // Intermediate Funcs
    Func as_float, resized_x, resized_y,
        unnormalized_kernel_x, unnormalized_kernel_y,
        kernel_x, kernel_y,
        kernel_sum_x, kernel_sum_y;

    void generate() {

        // Handle different types by just casting to float
        as_float(x, y, c) = cast<float>(input(x, y, c));

        // For downscaling, widen the interpolation kernel to perform lowpass
        // filtering.

        // Invert the scale factor in a single place and do it
        // strictly, to avoid getting different ratios showing up in
        // different places.
        Expr inverse_scale_factor = strict_float(1.0f / scale_factor);

        Expr kernel_scaling = upsample ? Expr(1.0f) : scale_factor;
        Expr inverse_kernel_scaling = upsample ? Expr(1.0f) : inverse_scale_factor;

        Expr kernel_radius = 0.5f * kernel_info[interpolation_type].taps * inverse_kernel_scaling;

        Expr kernel_taps = cast<int>(ceil(kernel_info[interpolation_type].taps * inverse_kernel_scaling));

        // source[xy] are the (non-integer) coordinates inside the source image
        Expr sourcex = (x + 0.5f) * inverse_scale_factor - 0.5f;
        Expr sourcey = (y + 0.5f) * inverse_scale_factor - 0.5f;

        // Initialize interpolation kernels. Since we allow an
        // arbitrary scaling factor, the filter coefficients are
        // different for each x and y coordinate. Use strict-float to
        // ensure fast-math doesn't mess up our bounds inference.
        Expr beginx = cast<int>(strict_float(ceil(sourcex - kernel_radius)));
        Expr beginy = cast<int>(strict_float(ceil(sourcey - kernel_radius)));
        beginx = clamp(beginx, input.dim(0).min(), input.dim(0).max() + 1 - kernel_taps);
        beginy = clamp(beginy, input.dim(1).min(), input.dim(1).max() + 1 - kernel_taps);

        RDom r(0, kernel_taps);
        const KernelInfo &info = kernel_info[interpolation_type];

        unnormalized_kernel_x(x, k) = info.kernel((k + beginx - sourcex) * kernel_scaling);
        unnormalized_kernel_y(y, k) = info.kernel((k + beginy - sourcey) * kernel_scaling);

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
        const int vec = natural_vector_size<float>();

        Var xi, yi;
        unnormalized_kernel_x
            .compute_at(kernel_x, x)
            .vectorize(x);
        kernel_sum_x
            .compute_at(kernel_x, x)
            .vectorize(x);
        kernel_x
            .compute_root()
            .reorder(k, x)
            .vectorize(x, vec);

        unnormalized_kernel_y
            .compute_at(kernel_y, y)
            .vectorize(y, vec);
        kernel_sum_y
            .compute_at(kernel_y, y)
            .vectorize(y);
        kernel_y
            .compute_at(output, y)
            .reorder(k, y)
            .vectorize(y, vec);

        if (upsample) {
            output
                .tile(x, y, xi, yi, 16, 64)
                .parallel(y)
                .vectorize(xi);
            resized_x
                .compute_at(output, x)
                .store_in(MemoryType::Stack)
                .vectorize(x);
            resized_y
                .compute_at(output, xi)
                .unroll(c);
        } else {
            output
                .tile(x, y, xi, yi, 32, 8)
                .parallel(y)
                .vectorize(xi);
            resized_y
                .compute_at(output, y)
                .vectorize(x, vec);
            resized_x
                .compute_at(output, xi)
                .unroll(c);
        }

        // Allow the input and output to have arbitrary memory layout,
        // and add some specializations for a few common cases. If
        // your case is not covered (e.g. planar input, packed rgb
        // output), you could add a new specialization here.
        output.dim(0).set_stride(Expr());
        input.dim(0).set_stride(Expr());

        Expr planar = (output.dim(0).stride() == 1 &&
                       input.dim(0).stride() == 1);
        Expr packed_rgb = (output.dim(0).stride() == 3 &&
                           output.dim(2).stride() == 1 &&
                           output.dim(2).min() == 0 &&
                           output.dim(2).extent() == 3 &&
                           input.dim(0).stride() == 3 &&
                           input.dim(2).stride() == 1 &&
                           input.dim(2).min() == 0 &&
                           input.dim(2).extent() == 3);
        Expr packed_rgba = (output.dim(0).stride() == 4 &&
                            output.dim(2).stride() == 1 &&
                            output.dim(2).min() == 0 &&
                            output.dim(2).extent() == 4 &&
                            input.dim(0).stride() == 4 &&
                            input.dim(2).stride() == 1 &&
                            input.dim(2).min() == 0 &&
                            input.dim(2).extent() == 4);

        output.specialize(planar);

        output.specialize(packed_rgb)
            .reorder(c, xi, yi, x, y)
            .unroll(c);

        output.specialize(packed_rgba)
            .reorder(c, xi, yi, x, y)
            .unroll(c);
    }
};

HALIDE_REGISTER_GENERATOR(Resize, resize);
