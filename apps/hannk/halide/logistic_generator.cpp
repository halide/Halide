#include "Halide.h"
#include "common_halide.h"

using namespace Halide;
using namespace Halide::ConciseCasts;

namespace hannk {

// Approximate log2(1 + exp2(x/2^log2_precision-x))*2^log2_precision_result
Expr approx_log2_1p_exp2(Expr x, Expr log2_precision_x, int log2_precision_result) {
    const int log2_p = 12;
    const int p = 1 << log2_p;
    Expr correction = -(1 << log2_precision_x) / 15;
    Expr one_plus_exp2_x = p + approx_exp2(x + correction, log2_precision_x, log2_p);

    // If we compute the log2 of the squared value, we can get a bit more precision.
    Expr one_plus_exp2_x_sq = pow(one_plus_exp2_x, 2);

    Expr raw = approx_log2(one_plus_exp2_x_sq, log2_precision_result - 1);

    // Since we computed log2(x*p) = log2(x) + log2(p), subtract log2(p) now.
    raw = raw - (log2_p << log2_precision_result);

    // For large x, the intermediate overflows. But log2(1 + 2^x) when x is large is just a line.
    Expr line = rounding_shift_right(x, log2_precision_x - log2_precision_result);
    Expr threshold = 5 << log2_precision_x;
    return select(x < threshold, raw, line);
}

class Logistic : public Generator<Logistic> {
public:
    Input<Buffer<uint8_t>> input_{"input", 1};

    Input<uint8_t> input_zero_{"input_zero"};
    Input<int32_t> input_multiplier_{"input_multiplier"};
    Input<uint32_t> input_shift_{"input_shift"};

    Output<Buffer<uint8_t>> output_{"output", 1};

    void generate() {
        // The algorithm.
        Var x("x");

        Expr input = i32(i16(input_(x)) - i16(input_zero_)) << 22;
        input = multiply_2x_high(input, input_multiplier_);

        // TODO: This is not very accurate. Improve it.
        const int log2_precision = 12;
        Expr log2_inv_logistic = approx_log2_1p_exp2(-input, input_shift_, log2_precision);
        const int correction = -(1 << log2_precision) / 15;
        Expr logistic = approx_exp2(-log2_inv_logistic + correction, log2_precision, 8);

        output_(x) = u8_sat(logistic);

        // Schedule.
        const int vector_size = natural_vector_size<uint8_t>();

        output_.vectorize(x, vector_size, TailStrategy::Predicate);
    }
};

}  // namespace hannk

HALIDE_REGISTER_GENERATOR(hannk::Logistic, Logistic)
