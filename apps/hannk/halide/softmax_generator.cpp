#include "Halide.h"
#include "common_halide.h"

using namespace Halide;
using namespace ConciseCasts;

namespace hannk {

class Softmax : public Generator<Softmax> {
public:
    Input<uint32_t> left_shift_{"left_shift"};

    Input<Buffer<uint8_t>> input_{"input", 2};
    Input<int32_t> beta_multiplier_{"beta_multiplier"};
    Input<uint32_t> beta_shift_{"beta_shift"};

    Input<uint8_t> output_offset_{"output_offset"};
    Input<int32_t> output_multiplier_{"output_multiplier"};
    Input<uint32_t> output_shift_{"output_shift"};
    Output<Buffer<uint8_t>> output_{"output", 2};

    void generate() {
        // The algorithm.
        Var x("x"), y("y");

        // On x86, this fixed point approximation is actually much slower
        // than just using floats, but producing identical results on all
        // targets is nice, and this op doesn't appear significant.

        // Compute 2^input_(x, y) / sum(2^input_(rx, y)) by rewriting it
        // to 2^(input_(x, y) - max_x(y)) / sum(2^(input_(rx, y) - max_x(y)).
        // This makes it easier to compute in fixed point, because we know
        // that 2^x is less than 1.
        RDom rx(0, input_.dim(0).extent());
        Func max_x("max_in_row");
        max_x(y) = 0;
        max_x(y) = max(input_(rx, y), max_x(y));

        Expr diff = i16(input_(x, y)) - i16(max_x(y));
        diff = i32(diff) << left_shift_;
        Expr diff_beta = multiply_2x_high(diff, beta_multiplier_);

        // Since we know that diff_beta is less than 0, we can use the full
        // range of an integer for the fractional part.
        const int exp_precision = 15;
        Func exp2_diff("exp2_diff");
        exp2_diff(x, y) = i16_sat(approx_exp2(diff_beta, beta_shift_, exp_precision));

        Func sum_exp_row("sum_exp_row");
        sum_exp_row(y) += i32(exp2_diff(rx, y));

        Expr output = (i32(exp2_diff(x, y)) << 16) / sum_exp_row(y);
        output = multiply_quantized(output, output_multiplier_, output_shift_);
        output = saturating_add(i16_sat(output), output_offset_);
        output_(x, y) = u8_sat(output);

        // Schedule.
        // TODO: This schedule has very little ILP, but the extent of y
        // is often 1.
        max_x.compute_at(output_, y)
            .update()
            .atomic()
            .vectorize(rx, natural_vector_size<uint8_t>());

        sum_exp_row.compute_at(output_, y)
            .update()
            .atomic()
            .vectorize(rx, natural_vector_size<uint8_t>());

        output_.vectorize(x, natural_vector_size<uint8_t>());
    }
};

}  // namespace hannk

HALIDE_REGISTER_GENERATOR(hannk::Softmax, Softmax)
