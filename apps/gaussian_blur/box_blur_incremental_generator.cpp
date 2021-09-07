#include "Halide.h"

using namespace Halide;

// This generator is only responsible for producing N scanlines of output
class BoxBlurIncremental : public Generator<BoxBlurIncremental> {
public:
    // The 8-bit input
    Input<Buffer<uint8_t>> input{"input", 2};

    // The input, already blurred in y and sum-scanned in x, for the N scanlines above the
    // one we're responsible for producing. Stored transposed.
    Input<Buffer<uint32_t>> prev_blur_y{"prev_blur_y", 1};
    Input<bool> prev_blur_y_valid{"prev_blur_y_valid"};
    Input<int> radius{"radius"};
    Input<int> width{"width"};

    Output<Buffer<uint32_t>> blur_y{"blur_y", 1};
    Output<Buffer<uint8_t>> output{"output", 1};

    void vectorized_sum_scan(Func out, Func in, Expr width, int vec, Expr condition) {
        assert((vec & (vec - 1)) == 0);  // vec must be a power of two

        assert(vec == 16);

        Var xo, xi0, xi1, xi2, xi3;
        Func in_vecs;
        in_vecs(xi0, xi1, xi2, xi3, xo) = cast<uint32_t>(in(xi0 + xi1 * 2 + xi2 * 4 + xi3 * 8 + xo * 16));

        RDom r2(0, 2, 0, 2, 0, 2, 0, 2);
        in_vecs(r2[0], r2[1], r2[2], r2[3], xo) += select(r2[0] == 0,
                                                          cast<uint32_t>(0),
                                                          in_vecs(0, r2[1], r2[2], r2[3], xo));
        in_vecs(r2[0], r2[1], r2[2], r2[3], xo) += select(r2[1] == 0,
                                                          cast<uint32_t>(0),
                                                          in_vecs(1, 0, r2[2], r2[3], xo));
        in_vecs(r2[0], r2[1], r2[2], r2[3], xo) += select(r2[2] == 0,
                                                          cast<uint32_t>(0),
                                                          in_vecs(1, 1, 0, r2[3], xo));
        in_vecs(r2[0], r2[1], r2[2], r2[3], xo) += select(r2[3] == 0,
                                                          cast<uint32_t>(0),
                                                          in_vecs(1, 1, 1, 0, xo));

        RDom r(0, 2,
               0, 2,
               0, 2,
               0, 2,
               0, width / vec);
        r.where(condition);

        out(r[0] + r[1] * 2 + r[2] * 4 + r[3] * 8 + r[4] * 16) =
            in_vecs(r[0], r[1], r[2], r[3], r[4]) + out(r[4] * vec - 1);

        out
            .update(out.num_update_definitions() - 1)
            .allow_race_conditions()
            .vectorize(r[0])
            .vectorize(r[1])
            .vectorize(r[2])
            .vectorize(r[3]);

        in.compute_at(out, r[4]).vectorize(in.args()[0]);
        in_vecs.compute_at(out, r[4])
            .vectorize(xi0)
            .vectorize(xi1)
            .vectorize(xi2)
            .vectorize(xi3);

        for (int i = 0; i < in_vecs.num_update_definitions(); i++) {
            in_vecs
                .update(i)
                .allow_race_conditions()
                .vectorize(r2[0])
                .vectorize(r2[1])
                .vectorize(r2[2])
                .vectorize(r2[3]);
        }

        // TODO: tail
    }

    void generate() {
        Var x{"x"}, y{"y"};

        const int vec = 16;

        Expr diameter = cast<uint32_t>(2 * radius + 1);

        // First update prev_blur_y
        Func delta{"delta"};
        delta(x) =
            cast<uint32_t>(prev_blur_y(x) - prev_blur_y(x - 1) +
                           (cast<int16_t>(input(x, 2 * radius)) - input(x, -1)));

        Func blur_y_direct{"blur_y_direct"};
        RDom rb(0, cast<int>(diameter));
        blur_y_direct(x) = cast<uint32_t>(0);
        blur_y_direct(x) += cast<uint32_t>(input(x, rb));

        // The input, blurred in y and sum-scanned in x at this output
        blur_y(x) = undef<uint32_t>();
        blur_y(-1) = cast<uint32_t>(0);

        vectorized_sum_scan(blur_y, delta, width + 2 * radius, 16, prev_blur_y_valid);
        vectorized_sum_scan(blur_y, lambda(x, blur_y_direct(x)), width + 2 * radius, 16, !prev_blur_y_valid);

        Func dithered{"dithered"};
        Expr result_32 = blur_y(x + diameter - 1) - blur_y(x - 1);

        auto normalize = [&](Expr num) {
            Expr den = diameter * diameter;
            return cast<uint8_t>(round(cast<int32_t>(num) * (1.0f / den)));
        };

        output(x) = normalize(result_32);

        blur_y_direct.compute_root()
            .vectorize(x, vec, TailStrategy::GuardWithIf)
            .update()
            .reorder(x, rb)
            .vectorize(x, vec, TailStrategy::GuardWithIf);

        output.vectorize(x, vec, TailStrategy::GuardWithIf);
    }
};

HALIDE_REGISTER_GENERATOR(BoxBlurIncremental, box_blur_incremental)
