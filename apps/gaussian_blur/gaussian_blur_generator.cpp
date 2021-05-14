// This file defines a generator for a first order IIR low pass filter
// for a 2D image.

#include "Halide.h"

using namespace Halide;
using namespace Halide::BoundaryConditions;

class GaussianBlur : public Generator<GaussianBlur> {
public:
    Input<Buffer<uint8_t>> input{"input", 2};
    Input<float> sigma{"sigma"};

    Output<Buffer<uint8_t>> output{"output", 2};

    Var x{"x"}, y{"y"};

    Func blur_cols_transpose(Func in, Expr height, Expr radius) {

        Func blur32, blur64;
        RDom ry(0, height + radius * 3);

        for (int bits : {32, 64}) {

            Func blur{"blur_" + std::to_string(bits)};

            Type t = UInt(bits);

            // This is going to work up to radius 256, after which we'll
            // get overflow.
            Expr scale = pow(cast(t, radius), 3);

            // Pure definition: do nothing.
            blur(x, y) = undef(t);

            // Update 0-2: set the top row of the result to the input.
            blur(x, -1) = scale * cast(t, in(x, 0));  // Tracks output
            blur(x, -2) = blur(x, -1);
            blur(x, -3) = blur(x, -1);

            Func in16;
            in16(x, y) = cast<int16_t>(in(x, y));

            // A Gaussian blur can be done as an IIR filter. The taps on
            // the input are 1, -3, 3, 1, spaced apart by the radius. The
            // taps on the previous three outputs are 3, -3, 1. The input
            // taps represent the third derivative of the kernel you get
            // if you iterate a box filter three times, and the taps on
            // the output are effectively triple integration of that
            // result. The following expression computes this IIR, nested
            // such that there's only one multiplication by three. Values
            // that have just been upcast from 8 to 16-bit are nested
            // together so that widening subtracts can be used on
            // architectures that support them (e.g. ARM).
            Expr v = (blur(x, ry - 3) +
                      (in16(x, ry) -
                       in16(x, ry - radius * 3)) +
                      3 * ((blur(x, ry - 1) -
                            blur(x, ry - 2)) +
                           (in16(x, ry - radius * 2) -
                            in16(x, ry - radius))));

            // Sign-extend then treat it as a uint32 with wrap-around. We
            // know that the result can't possibly be negative in the end,
            // so this gives us an extra bit of headroom while
            // accumulating.
            v = cast(UInt(bits), cast(Int(bits), v));

            // Update 3
            blur(x, ry) = v;

            if (bits == 32) {
                blur32 = blur;
            } else {
                blur64 = blur;
            }
        }

        Func blur;
        blur(x, y) = select(radius >= 256, cast<float>(blur64(x, y)), cast<float>(blur32(x, y)));

        // Transpose the blur and normalize.
        Func transpose("transpose");

        Expr inv_scale = 1.0f / pow(cast<float>(radius), 3);

        transpose(x, y) = cast<uint8_t>(round(clamp(blur(y, x + (radius * 3) / 2 - 1) * inv_scale, 0.0f, 255.0f)));

        const int vec = get_target().natural_vector_size<uint8_t>();

        // CPU schedule.  Split the transpose into tiles of
        // rows. Parallelize over strips.
        Var xo, yo, xi, yi, strip;
        transpose.compute_root()
            .tile(x, y, xo, yo, x, y, vec, vec)
            .vectorize(x)
            .reorder(x, y, xo, yo)
            .parallel(yo);

        for (Func b : {blur32, blur64}) {
            // Run the filter on each row of tiles (which corresponds to a strip of
            // columns in the input).
            b.compute_at(transpose, yo);

            for (int i = 0; i < 3; i++) {
                b.update(i).vectorize(x, vec);
            }

            // Vectorize computations within the strips.
            b.update(3)
                .reorder(x, ry)
                .vectorize(x, vec);
        }

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
    }
};

HALIDE_REGISTER_GENERATOR(GaussianBlur, gaussian_blur)

#include "Halide.h"

using namespace Halide;
using namespace Halide::BoundaryConditions;

class GaussianBlurDirect : public Generator<GaussianBlurDirect> {
public:
    Input<Buffer<uint8_t>> input{"input", 2};
    Input<float> sigma{"sigma"};

    Output<Buffer<uint8_t>> output{"output", 2};

    Var x{"x"}, y{"y"};

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
        blur_y_32(x, y) +=
            (cast<int32_t>(kernel_normalized_2(r)) *
             (cast<int16_t>(clamped(x, y + r)) +
              clamped(x, y - r)));
        blur_y(x, y) = cast<uint8_t>((blur_y_32(x, y) + scale) / (2 * scale));
        blur_x_32(x, y) +=
            (cast<int32_t>(kernel_normalized_2(r)) *
             (cast<int16_t>(blur_y(x + r, y)) +
              blur_y(x - r, y)));
        blur_x(x, y) = cast<uint8_t>((blur_x_32(x, y) + scale) / (2 * scale));

        output = blur_x;

        const int vec = natural_vector_size<uint8_t>();

        Var yo, yi;
        blur_x.compute_root()
            .reorder(x, y)
            .split(y, yo, yi, 64, TailStrategy::GuardWithIf)
            .vectorize(x, vec)
            .parallel(yo);

        blur_y.compute_at(blur_x, yo)
            .vectorize(x, vec);

        clamped.store_at(blur_x, yo)
            .compute_at(blur_y, y)
            .vectorize(_0, vec);
    }
};

HALIDE_REGISTER_GENERATOR(GaussianBlurDirect, gaussian_blur_direct)
