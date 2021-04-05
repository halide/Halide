#include "Halide.h"
#include "common_halide.h"

using namespace Halide;
using namespace Halide::ConciseCasts;

namespace hannk {

class L2Normalization : public Generator<L2Normalization> {
public:
    Input<Buffer<uint8_t>> input_{"input", 2};

    Input<uint8_t> input_zero_{"input_zero"};

    Output<Buffer<uint8_t>> output_{"output", 2};

    void generate() {
        Var x("x"), y("y");

        // We don't need the input scale, because the result of L2
        // normalization doesn't depend on it.
        Func input_zeroed("input_zeroed");
        input_zeroed(x, y) = i16(input_(x, y)) - i16(input_zero_);

        Func sum_input_sq("sum_input_sq");
        RDom rx(input_.dim(0).min(), input_.dim(0).extent());
        sum_input_sq(y) = i32(0);
        sum_input_sq(y) += pow(i32(input_zeroed(rx, y)), 2);

        // Compute 1 / sqrt(x) = 2^log2(x^(-1/2)) = 2^((-1/2)*log2(x))
        // TODO: Are our approx_log2/approx_exp2 precise enough for this op?
        Func inv_sqrt("inv_sqrt");
        const int log2_precision = 16;
        const int exp2_precision = 16;
        Expr log2_sum_input_sq = approx_log2(sum_input_sq(y), log2_precision);
        inv_sqrt(y) =
            approx_exp2(-log2_sum_input_sq, log2_precision + 1, exp2_precision);
        // approx_exp2 linearly interpolates the exact powers of 2. Since
        // 2^x is concave-up, this approximation consistently overestimates
        // the true function. Roughly correct this error with this correction
        // factor.
        inv_sqrt(y) = (inv_sqrt(y) * 15 + 8) / 16;

        // The output has a scale of 2^7 = 128 and offset of 128.
        Expr output_scaled = i32(input_zeroed(x, y)) * i32(inv_sqrt(y));
        output_(x, y) =
            u8_sat(rounding_shift_right(output_scaled, exp2_precision - 7) + 128);

        // Schedule.
        const int vector_size = natural_vector_size<uint8_t>();

        output_.compute_root()
            .vectorize(x, vector_size, TailStrategy::GuardWithIf);

        inv_sqrt.compute_at(output_, y);

        sum_input_sq.compute_at(output_, y)
            .update()
            .atomic()
            .vectorize(rx, vector_size);
    }
};

}  // namespace hannk

HALIDE_REGISTER_GENERATOR(hannk::L2Normalization, L2Normalization)
