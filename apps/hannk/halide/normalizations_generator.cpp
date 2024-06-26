#include "Halide.h"
#include "halide/common_halide.h"
#include "halide/constants.h"

using namespace Halide;
using namespace Halide::ConciseCasts;

namespace hannk {

class L2Normalization : public Generator<L2Normalization> {
public:
    Input<Buffer<uint8_t, 2>> input_{"input"};
    Input<uint8_t> input_zero_{"input_zero"};

    Output<Buffer<uint8_t, 2>> output_{"output"};

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

        Func inv_sqrt("inv_sqrt");
        const int q = 15;
        inv_sqrt(y) = approx_reciprocal_sqrt(q, sum_input_sq(y));

        // The output has a scale of 2^7 = 128 and offset of 128.
        Expr output = i32(input_zeroed(x, y)) * i32(inv_sqrt(y));
        output = i16_sat(rounding_shift_right(output, q - 7));
        output_(x, y) = u8_sat(saturating_add(output, i16(128)));

        // Schedule.
        const int vector_size = natural_vector_size<uint8_t>();

        output_.compute_root()
            .vectorize(x, vector_size, TailStrategy::Predicate);

        inv_sqrt.compute_at(output_, y);

        sum_input_sq.compute_at(output_, y)
            .update()
            .atomic()
            .vectorize(rx, vector_size);

        // Normally we'd expect both buffers to be planar, but in unusual
        // cases, Hannk can transpose the buffers (to normalize along another
        // dimension), so for those cases, we'll just fall back to less-efficient
        // code.
        input_.dim(0).set_stride(Expr());
        output_.dim(0).set_stride(Expr());
        output_.specialize(input_.dim(0).stride() == 1 && output_.dim(0).stride() == 1);
    }
};

class Softmax : public Generator<Softmax> {
public:
    Input<Buffer<uint8_t, 2>> input_{"input"};
    // The beta multiplier and shift should have an extra factor of log2(e).
    Input<int16_t> beta_multiplier_{"beta_multiplier"};
    Input<uint16_t> beta_shift_{"beta_shift"};

    Input<uint8_t> output_zero_{"output_zero"};
    Input<int16_t> output_multiplier_{"output_multiplier"};
    Input<uint16_t> output_shift_{"output_shift"};
    Output<Buffer<uint8_t, 2>> output_{"output"};

    void generate() {
        // The algorithm.
        Var x("x"), y("y");

        // On x86, this fixed point approximation is actually much slower
        // than just using floats, but producing identical results on all
        // targets is nice, and this op doesn't appear to be a significant
        // factor in overall performance.

        // Compute 2^input_(x, y) / sum(2^input_(rx, y)) by rewriting it
        // to 2^(input_(x, y) - max_x(y)) / sum(2^(input_(rx, y) - max_x(y)).
        // This makes it easier to compute in fixed point, because we know
        // that 2^x is less than 1.
        Func max_x("max_x");
        RDom rx(input_.dim(0).min(), input_.dim(0).extent());
        max_x(y) = u8(0);
        max_x(y) = max(max_x(y), input_(rx, y));

        Expr diff = (i16(input_(x, y)) - i16(max_x(y))) << softmax_input_shift;
        Expr diff_beta = multiply_2x_high(diff, beta_multiplier_);

        // Since we know that diff_beta is less than 0, we can use the full
        // range of an integer for the fractional part.
        constexpr int q = 15;
        Func exp2_diff("exp2_diff");
        exp2_diff(x, y) = approx_exp2(q, diff_beta, beta_shift_, Int(16));

        // This could overflow if there are more than 2^16 values of x.
        Func sum_exp_row("sum_exp_row");
        sum_exp_row(y) += i32(exp2_diff(rx, y));

        // Below, we compute exp2_diff * inv_sum_exp_row / 2^15, so we need to
        // multiply by 2^(q + 15) to get a result of the correct quantization.
        // This doesn't overflow because we know the sum is greater than or equal
        // to 2^0*2^q, because we subtracted the max from the input.
        Func inv_sum_exp_row("inv_sum_exp_row");
        inv_sum_exp_row(y) = approx_reciprocal(q + 15, sum_exp_row(y), Int(16));

        static_assert(q == 15, "");
        Expr output = multiply_2x_high(exp2_diff(x, y), inv_sum_exp_row(y));
        output = multiply_2x_high(output, output_multiplier_);
        output = rounding_shift_right(output, output_shift_);
        output_(x, y) = u8_sat(saturating_add(output, output_zero_));

        // Schedule.
        const int vector_size = natural_vector_size<uint8_t>();

        output_.vectorize(x, vector_size, TailStrategy::Predicate);

        max_x.compute_at(output_, y)
            .update()
            .atomic()
            .vectorize(rx, vector_size, TailStrategy::GuardWithIf);

        sum_exp_row.compute_at(output_, y)
            .update()
            .atomic()
            .vectorize(rx, vector_size, TailStrategy::GuardWithIf);

        inv_sum_exp_row.compute_at(output_, y);

        // Normally we'd expect both buffers to be planar, but in unusual
        // cases, Hannk can transpose the buffers (to normalize along another
        // dimension), so for those cases, we'll just fall back to less-efficient
        // code.
        input_.dim(0).set_stride(Expr());
        output_.dim(0).set_stride(Expr());
        output_.specialize(input_.dim(0).stride() == 1 && output_.dim(0).stride() == 1);
    }
};

}  // namespace hannk

HALIDE_REGISTER_GENERATOR(hannk::L2Normalization, L2Normalization)
HALIDE_REGISTER_GENERATOR(hannk::Softmax, Softmax)
