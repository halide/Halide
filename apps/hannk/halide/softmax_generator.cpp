#include "Halide.h"
#include "common_halide.h"

using namespace Halide;
using namespace ConciseCasts;

namespace hannk {

class Softmax : public Generator<Softmax> {
public:
    Input<uint32_t> left_shift_{"left_shift"};

    Input<Buffer<uint8_t>> input_{"input", 2};
    Input<int32_t> input_multiplier_{"input_multiplier"};
    Input<uint32_t> input_shift_{"input_shift"};

    Input<uint8_t> output_offset_{"output_offset"};
    Input<int32_t> output_multiplier_{"output_multiplier"};
    Input<uint32_t> output_shift_{"output_shift"};
    Output<Buffer<uint8_t>> output_{"output", 2};

    void generate() {
        // The algorithm.
        Var x("x"), y("y");

        RDom rx(0, input_.dim(0).extent());
        Func max_x("max_in_row");
        max_x(y) = maximum(input_(rx, y));

        Expr diff = i16(input_(x, y)) - i16(max_x(y));
        diff = i32(diff) << left_shift_;
        Expr diff_beta = multiply_2x_high(diff, input_multiplier_);

        const int sum_precision = 15;
        Func exp2_diff("exp2_diff");
        exp2_diff(x, y) = i16_sat(approx_exp2(diff_beta, input_shift_, sum_precision));

        Func sum_exp_row("sum_exp_row");
        sum_exp_row(y) += i32(exp2_diff(rx, y));

        Func inv_exp_sum_row("inv_exp_sum_row");
        inv_exp_sum_row(y) = i16_sat((1 << 30) / rounding_shift_right(sum_exp_row(y), 15));

        Expr output = widening_mul(exp2_diff(x, y), inv_exp_sum_row(y));
        output = multiply_quantized(output, output_multiplier_, output_shift_);
        output = saturating_add(i16_sat(output), output_offset_);
        output_(x, y) = u8_sat(output);

        max_x.compute_root();
        sum_exp_row.compute_root();
    }
};

}  // namespace hannk

HALIDE_REGISTER_GENERATOR(hannk::Softmax, Softmax)
