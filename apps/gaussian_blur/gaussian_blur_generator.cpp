// This file defines a generator for a first order IIR low pass filter
// for a 2D image.

#include "Halide.h"

using namespace Halide;
using namespace Halide::BoundaryConditions;

class GaussianBlur : public Generator<GaussianBlur> {
public:
    Input<Buffer<uint8_t>> input{"input", 3};
    Input<float> sigma{"sigma"};

    Output<Buffer<uint8_t>> output{"output", 3};

    Var x{"x"}, y{"y"}, c{"c"};

    Func blur_cols_transpose(Func in, Expr height, Expr radius) {
        Func blur{"blur"};

        Type t = UInt(32);

        // This is going to work up to radius 256, after which we'll
        // get overflow.
        Expr scale = cast(t, pow(radius, 3));

        Expr inv_scale = 1.0f / scale;

        // Pure definition: do nothing.
        blur(x, y, c) = undef(t);

        // Update 0-2: set the top row of the result to the input.
        blur(x, -1, c) = scale * cast(t, in(x, 0, c));  // Tracks output
        blur(x, -2, c) = cast(t, 0);                    // Tracks derviative of the output
        blur(x, -3, c) = cast(t, 0);                    // Tracks 2nd derivative of the output

        RDom ry(0, 4, 0, height + radius * 3);

        std::vector<Expr> lhs{-3, -2, ry.y};

        // Third derivative of an approximate Gaussian evaluated at ry.y
        Expr v = ((cast<int16_t>(in(x, ry.y, c)) -
                   in(x, ry.y - radius * 3, c)) +
                  3 * (cast<int16_t>(in(x, ry.y - radius * 2, c)) -
                       in(x, ry.y - radius, c)));

        // Sign-extend then treat it as a uint32 with wrap-around. We
        // know that the result can't possibly be negative in the end,
        // so this gives us an extra bit of headroom while
        // accumulating.
        v = cast<uint32_t>(cast<int32_t>(v));

        std::vector<Expr> rhs{
            // Update the second derivative using the third derviative
            blur(x, -3, c) + v,
            // Update the first derivative using the second derivative
            blur(x, -2, c) + blur(x, -3, c),
            // Update the previous output using the first derivative
            blur(x, ry.y - 1, c) + blur(x, -2, c)};

        // Update 3
        blur(x, mux(ry.x, lhs), c) = mux(ry.x, rhs);

        // Transpose the blur and normalize.
        Func transpose("transpose");

        transpose(x, y, c) = cast<uint8_t>(round(clamp(blur(y, x + (radius * 3) / 2 - 1, c) * inv_scale, 0.0f, 255.0f)));

        const int vec = get_target().natural_vector_size<uint8_t>();

        // CPU schedule.  Split the transpose into tiles of
        // rows. Parallelize over channels and strips.
        Var xo, yo, xi, yi, strip;
        transpose.compute_root()
            .tile(x, y, xo, yo, x, y, vec, vec)
            .vectorize(x)
            .reorder(x, y, xo, yo, c)
            .parallel(yo)
            .parallel(c);

        // Run the filter on each row of tiles (which corresponds to a strip of
        // columns in the input).
        blur.compute_at(transpose, yo);

        for (int i = 0; i < 3; i++) {
            blur.update(i).vectorize(x, vec);
        }

        // Vectorize computations within the strips.
        blur.update(3)
            .reorder(ry.x, x, ry.y)
            .unroll(ry.x)
            .vectorize(x, vec);

        // Load the input strip required in a pre-pass so that we
        // don't incur stalls due to memory latency when running the
        // IIR.
        in.in()
            .compute_at(transpose, yo)
            .vectorize(in.args()[0]);

        return transpose;
    }

    void generate() {

        // Convolve by the third derivative of a cubic approximation
        // to a Gaussian. This is equivalent to doing a box blur three
        // times.

        // We need to pick a radius for the box blur that achieves our
        // desired gaussian sigma. If that box blur has "r" taps,
        // then its variance is (r^2 - 1) / 12. Iterated three times
        // we get variance (r^2 - 1) / 4.  Solving v = (r^2 - 1)/4 for
        // r we get: r = sqrt(4v + 1)

        Expr variance = sigma * sigma;
        Expr radius = cast<int>(round(sqrt(4 * variance + 1)));

        Expr width = input.width();
        Expr height = input.height();

        Func clamped = BoundaryConditions::repeat_edge(input, {{0, width}, {0, height}});

        // First, blur the columns of the input.
        Func blury_T = blur_cols_transpose(clamped, height, radius);

        // Blur the columns again (the rows of the original).
        Func blur = blur_cols_transpose(blury_T, width, radius);

        output = blur;

        output.dim(2).set_bounds(0, 3);
    }
};

HALIDE_REGISTER_GENERATOR(GaussianBlur, gaussian_blur)

#include "Halide.h"

using namespace Halide;
using namespace Halide::BoundaryConditions;

class GaussianBlurDirect : public Generator<GaussianBlurDirect> {
public:
    Input<Buffer<uint8_t>> input{"input", 3};
    Input<float> sigma{"sigma"};

    Output<Buffer<uint8_t>> output{"output", 3};

    Var x{"x"}, y{"y"}, c{"c"};

    void generate() {

        Func kernel;
        kernel(x) = exp(-(x * x) / (2 * sigma * sigma));
        kernel(0) /= 2;
        kernel.compute_root();

        Expr radius = cast<int>(ceil(3 * sigma));
        RDom r(0, radius);

        Func kernel_sum;
        kernel_sum() = sum(kernel(r));
        kernel_sum.compute_root();

        const int32_t scale = 64 * 256;

        Func kernel_normalized_1;
        kernel_normalized_1(x) = cast<int16_t>(round(scale * kernel(x) / kernel_sum()));
        kernel_normalized_1.compute_root();
        // This kernel approximately adds up to 'scale'

        Func kernel_quantized_sum;
        kernel_quantized_sum() += kernel_normalized_1(r);
        kernel_quantized_sum.compute_root();

        // Make the kernel exactly add up to 'scale'
        Func kernel_normalized_2;
        Expr correction = cast<int16_t>(scale - kernel_quantized_sum());
        kernel_normalized_2(x) = cast<int16_t>(kernel_normalized_1(x) +
                                               select(x == 0, correction, 0));
        kernel_normalized_2.compute_root();

        Expr width = input.width();
        Expr height = input.height();
        Func clamped = BoundaryConditions::repeat_edge(input, {{0, width}, {0, height}});

        Func blur_y("blur_y"), blur_y_32("blur_y_32"), blur_x("blur_x"), blur_x_32("blur_x_32");
        blur_y_32(x, y, c) +=
            (cast<int32_t>(kernel_normalized_2(r)) *
             (cast<int16_t>(clamped(x, y + r, c)) +
              clamped(x, y - r, c)));
        blur_y(x, y, c) = cast<uint8_t>((blur_y_32(x, y, c) + scale) / (2 * scale));
        blur_x_32(x, y, c) +=
            (cast<int32_t>(kernel_normalized_2(r)) *
             (cast<int16_t>(blur_y(x + r, y, c)) +
              blur_y(x - r, y, c)));
        blur_x(x, y, c) = cast<uint8_t>((blur_x_32(x, y, c) + scale) / (2 * scale));

        output = blur_x;

        const int vec = natural_vector_size<uint8_t>();

        Var yo, yi;
        blur_x.compute_root()
            .reorder(x, c, y)
            .split(y, yo, yi, 64, TailStrategy::GuardWithIf)
            .vectorize(x, vec)
            .parallel(yo);

        /*
        blur_x_32
            .compute_at(blur_x, x)
            .vectorize(x)
            .update()
            .reorder(r, x, y, c)
            .atomic()
            .vectorize(r, 2)
            .vectorize(x);
        */

        blur_y.compute_at(blur_x, c)
            .vectorize(x, vec);

        /*
        blur_y_32
            .compute_at(blur_y, x)
            .vectorize(x)
            .update()
            .reorder(r, x, y, c)
            .atomic()
            .vectorize(r, 2)
            .vectorize(x);
        */

        clamped.store_at(blur_x, yo)
            .compute_at(blur_y, y)
            .vectorize(_0, vec);
    }
};

HALIDE_REGISTER_GENERATOR(GaussianBlurDirect, gaussian_blur_direct)
