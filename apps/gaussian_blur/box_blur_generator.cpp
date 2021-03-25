// This file defines a generator for a first order IIR low pass filter
// for a 2D image.

#include "Halide.h"

using namespace Halide;
using namespace Halide::BoundaryConditions;

class BoxBlur : public Generator<BoxBlur> {
public:
    Input<Buffer<uint8_t>> input{"input", 2};
    Input<int> radius{"radius"};
    Input<int> out_width{"out_width"}, out_height{"out_height"};
    Output<Buffer<uint8_t>> intermediate{"intermediate", 2};
    Output<Buffer<uint8_t>> output{"output", 2};

    Var x{"x"}, y{"y"};

    Func blur_cols_transpose(Func in, Expr height, bool first_pass) {
        Expr diameter = 2 * radius + 1;
        Expr inv_scale = 1.f / diameter;
        RDom r_init(-radius, diameter);
        RDom ry(1, height - 1);

        Func wrap("wrap");
        wrap(x, y) = in(x, y);

        // Transpose the input
        Func transpose("transpose");
        transpose(x, y) = wrap(y, x);

        // Blur in y
        std::vector<Func> blurs, dithered;
        for (Type t : {UInt(16), UInt(32)}) {
            Func blur{"blur_" + std::to_string(t.bits())};
            blur(x, y) = undef(t);
            blur(x, 0) = cast(t, 0);
            blur(x, 0) += cast(t, transpose(x, r_init));

            // Derivative of a box
            Expr v =
                (cast(Int(16), transpose(x, ry + radius)) -
                 transpose(x, ry - radius - 1));

            // It's a 9-bit signed integer. Sign-extend then treat it as a
            // uint16/32 with wrap-around. We know that the result can't
            // possibly be negative in the end, so this gives us an extra
            // bit of headroom while accumulating.
            v = cast(t, cast(Int(t.bits()), v));

            blur(x, ry) = blur(x, ry - 1) + v;

            blurs.push_back(blur);

            Func dither;
            dither(x, y) = cast<uint8_t>(floor(blur(x, y) * inv_scale + random_float()));
            //dither(x, y) = cast<uint8_t>(blur(x, y));
            dithered.push_back(dither);
        }

        const int vec = get_target().natural_vector_size<uint16_t>();

        Func out;
        out(x, y) = select(diameter < 256, dithered[0](x, y), dithered[1](x, y));

        // Schedule.  Split the transpose into tiles of
        // rows. Parallelize strips.
        Var xo, yo, xi, yi, xoo;
        out
            .compute_root()
            .split(x, xoo, xo, vec * 2)
            .split(xo, xo, xi, vec)
            .reorder(xi, y, xo, xoo)
            .vectorize(xi)
            .parallel(xoo);

        // Run the filter on each row of tiles (which corresponds to a strip of
        // columns in the input).
        for (int i = 0; i < 2; i++) {
            Func blur = blurs[i];
            Func dither = dithered[i];
            blur.compute_at(out, xo)
                .store_in(MemoryType::Stack);

            blur.update(0).vectorize(x);
            blur.update(1).vectorize(x);

            // Vectorize computations within the strips.
            blur.update(2)
                .reorder(x, ry)
                .vectorize(x);

            dither
                .compute_at(out, y)
                .vectorize(x);
        }

        transpose
            .compute_at(out, xo)
            .store_in(MemoryType::Stack)
            .split(y, yo, yi, vec)
            .unroll(x)
            .vectorize(yi);

        wrap
            .compute_at(transpose, yo)
            .store_in(MemoryType::Register)
            .vectorize(x)
            .unroll(y);

        out.specialize(diameter < 256);

        return out;
    }

    void generate() {
        // First, blur the columns of the input.
        Func blury_T = blur_cols_transpose(input, out_width, true);

        intermediate = blury_T;

        // Blur the columns again (the rows of the original).
        Func blur = blur_cols_transpose(blury_T, out_height, false);

        output = blur;
    }
};

HALIDE_REGISTER_GENERATOR(BoxBlur, box_blur)

class BoxBlurLog : public Generator<BoxBlurLog> {
public:
    Input<Buffer<uint8_t>> input{"input", 2};
    Input<int> radius{"radius"};
    Output<Buffer<uint8_t>> output{"output", 2};

    void generate() {
        Expr diameter = cast<uint32_t>(2 * radius + 1);
        Var x, y;
        Func clamped = BoundaryConditions::repeat_edge(input);

        Func in16;
        in16(x, y) = cast<uint16_t>(clamped(x, y));

        // Assume diameter < 256
        std::vector<Func> horiz_blurs, vert_blurs;
        Expr result = in16(x, y - radius);
        Expr offset = -radius + 1;
        Func prev = in16;
        for (int i = 0; i < 8; i++) {
            Func next("blur_y_" + std::to_string(1 << i));
            next(x, y) = prev(x, y) + prev(x, y + (1 << i));
            prev = next;
            vert_blurs.push_back(next);

            Expr use_this = ((diameter >> (i + 1)) & 1) == 1;
            result += select(use_this, next(x, y + offset), 0);
            offset += select(use_this, (1 << i), 0);
        }

        Func blur_y;
        blur_y(x, y) = cast<uint8_t>(clamp((result + diameter / 2) / diameter, 0, 255));

        horiz_blurs.push_back(blur_y);

        result = blur_y(x - radius, y);
        offset = -radius + 1;
        prev = blur_y;
        for (int i = 0; i < 8; i++) {
            Func next("blur_x_" + std::to_string(1 << i));
            next(x, y) = prev(x, y) + prev(x + (1 << i), y);
            prev = next;
            horiz_blurs.push_back(next);

            Expr use_this = ((diameter >> (i + 1)) & 1) == 1;
            result += select(use_this, next(x + offset, y), 0);
            offset += select(use_this, (1 << i), 0);
        }

        output(x, y) = cast<uint8_t>(clamp((result + diameter / 2) / diameter, 0, 255));

        Var yi, yo;
        output
            .vectorize(x, natural_vector_size<uint8_t>())
            .split(y, yo, yi, 64, TailStrategy::GuardWithIf)
            .parallel(yo);

        clamped.compute_at(output, yo).vectorize(_0, natural_vector_size<uint8_t>());

        for (Func b : vert_blurs) {
            b
                .compute_at(output, yo)
                .store_in(MemoryType::Stack)
                .vectorize(x, natural_vector_size<uint16_t>());
        }

        for (Func b : horiz_blurs) {
            b
                .compute_at(output, yi)
                .store_in(MemoryType::Stack)
                .vectorize(x, natural_vector_size<uint16_t>());
        }
    }
};

HALIDE_REGISTER_GENERATOR(BoxBlurLog, box_blur_log)
