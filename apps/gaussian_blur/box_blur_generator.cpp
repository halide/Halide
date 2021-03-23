// This file defines a generator for a first order IIR low pass filter
// for a 2D image.

#include "Halide.h"

using namespace Halide;
using namespace Halide::BoundaryConditions;


#if 0
class BoxBlur : public Generator<BoxBlur> {
public:
    Input<Buffer<uint8_t>> input{"input", 2};
    Input<int> radius{"radius"};
    Output<Buffer<uint8_t>> output{"output", 2};

    Var x{"x"}, y{"y"};

    Func blur_cols_transpose(Func in, Expr height) {
        std::vector<Func> blurs;

        Expr diameter = 2*radius + 1;
        Expr inv_scale = 1.f / diameter;
        RDom ry(-radius, height + diameter);

        for (int working_bits : {16, 32}) {
            Func blur{"blur_" + std::to_string(working_bits)};

            Type t = UInt(working_bits);

            // Pure definition: do nothing.
            blur(x, y) = undef(t);

            // The initial value of the accumulator should match the
            // boundary condition we are using on the input, or the
            // subtractions of the off-edge values done below will be
            // incorrect.
            blur(x, -radius - 1) = cast(t, diameter) * cast(t, in(x, 0));

            // Derivative of a box
            Expr v = cast<int16_t>(in(x, ry)) - in(x, ry - diameter);

            // It's a 9-bit signed integer. Sign-extend then treat it as a
            // uint16/32 with wrap-around. We know that the result can't
            // possibly be negative in the end, so this gives us an extra
            // bit of headroom while accumulating.
            v = cast(t, cast(Int(working_bits), v));

            blur(x, ry) = blur(x, ry - 1) + v;

            blurs.push_back(blur);
        }

        // Select the appropriate blur for the radius. The other will
        // be skipped.
        Func blur;
        blur(x, y) = select(diameter < 256,
                            cast(Int(16), blurs[0](x, y)) * inv_scale,
                            cast(Int(32), blurs[1](x, y)) * inv_scale);

        // Transpose the blur and normalize.
        Func transpose("transpose");
        transpose(x, y) = cast<uint8_t>(round(clamp(blur(y, x), 0.f, 255.f)));

        const int vec = get_target().natural_vector_size<uint8_t>();

        // Schedule.  Split the transpose into tiles of
        // rows. Parallelize strips.
        Var xo, yo, xi, yi, strip;
        transpose.compute_root()
            .tile(x, y, xo, yo, x, y, vec, vec)
            .vectorize(y)
            .unroll(x)
            .reorder(x, y, xo, yo)
            .parallel(yo);

        for (Func blur : blurs) {
            // Run the filter on each row of tiles (which corresponds to a strip of
            // columns in the input).
            blur.compute_at(transpose, yo);

            blur.update(0).vectorize(x, vec);

            // Vectorize computations within the strips.
            blur.update(1)
                .reorder(x, ry)
                .vectorize(x, vec);
        }

        // Load the input strip required in a pre-pass so that we
        // don't incur stalls due to memory latency when doing the
        // scan.
        /*
        in.in()
            .compute_at(transpose, yo)
            .vectorize(in.args()[0]);
        */
        return transpose;
    }

    void generate() {
        // TODO: should actually be the output size
        Expr width = input.width();
        Expr height = input.height();

        Func clamped = BoundaryConditions::repeat_edge(input);

        // First, blur the columns of the input.
        Func blury_T = blur_cols_transpose(clamped, height);

        // Blur the columns again (the rows of the original).
        Func blur = blur_cols_transpose(blury_T, width);

        output = blur;
    }
};
#endif

class BoxBlur : public Generator<BoxBlur> {
public:
    Input<Buffer<uint8_t>> input{"input", 2};
    Input<int> radius{"radius"};
    Output<Buffer<uint32_t>> intermediate{"intermediate", 2};
    Output<Buffer<uint8_t>> output{"output", 2};

    Var x{"x"}, y{"y"};

    Func blur_cols_transpose(Func in, Expr height, bool first_pass) {
        Expr diameter = 2*radius + 1;
        Expr inv_scale = 1.f / (diameter * diameter);
        RDom ry(-radius, height + diameter);

        Func blur{"blur"};

        Type t = UInt(32);

        // Pure definition: do nothing.
        blur(x, y) = undef(t);

        // The initial value of the accumulator should match the
        // boundary condition we are using on the input, or the
        // subtractions of the off-edge values done below will be
        // incorrect.
        blur(x, -radius - 1) = cast(t, diameter) * cast(t, in(x, 0));

        // Derivative of a box
        Expr v = cast(Int(first_pass ? 16 : 32), in(x, ry)) - in(x, ry - diameter);

        // It's a 9-bit signed integer. Sign-extend then treat it as a
        // uint16/32 with wrap-around. We know that the result can't
        // possibly be negative in the end, so this gives us an extra
        // bit of headroom while accumulating.
        v = cast(t, cast(Int(32), v));

        blur(x, ry) = blur(x, ry - 1) + v;

        // Transpose the blur and normalize.
        Func transpose("transpose");
        if (first_pass) {
            transpose(x, y) = blur(y, x);
        } else {
            transpose(x, y) = cast<uint8_t>(round(clamp(blur(y, x) * inv_scale, 0.f, 255.f)));
        }

        const int vec = get_target().natural_vector_size<uint32_t>();

        // Schedule.  Split the transpose into tiles of
        // rows. Parallelize strips.
        Var xo, yo, xi, yi, strip;
        transpose.compute_root()
            .tile(x, y, xo, yo, x, y, vec, vec)
            .vectorize(x)
            .unroll(y)
            .reorder(x, y, xo, yo)
            .parallel(yo);

        // Run the filter on each row of tiles (which corresponds to a strip of
        // columns in the input).
        blur.compute_at(transpose, yo)
            .store_in(MemoryType::Stack);

        blur.update(0).vectorize(x, vec);

        // Vectorize computations within the strips.
        blur.update(1)
            .reorder(x, ry)
            .vectorize(x, vec);

        blur.in().compute_at(transpose, xo)
            .reorder_storage(y, x)
            .vectorize(x)
            .unroll(y);

        return transpose;
    }

    void generate() {
        // TODO: should actually be the output size
        Expr width = input.width();
        Expr height = input.height();

        Func clamped = BoundaryConditions::repeat_edge(input);

        // First, blur the columns of the input.
        Func blury_T = blur_cols_transpose(clamped, height, true);

        intermediate = blury_T;

        // Blur the columns again (the rows of the original).
        Func blur = blur_cols_transpose(blury_T, width, false);

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
        Expr diameter = cast<uint32_t>(2*radius + 1);
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
        blur_y(x, y) = cast<uint8_t>(clamp((result + diameter/2) / diameter, 0, 255));

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

        output(x, y) = cast<uint8_t>(clamp((result + diameter/2) / diameter, 0, 255));

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

        output.specialize(radius == 2);
        //output.specialize_fail("poop");
        /*
        output.specialize(radius == 2);
        output.specialize(radius == 4);
        */
    }
};

HALIDE_REGISTER_GENERATOR(BoxBlurLog, box_blur_log)
