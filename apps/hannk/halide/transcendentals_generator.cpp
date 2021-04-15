#include "Halide.h"
#include "common_halide.h"

using namespace Halide;
using namespace Halide::ConciseCasts;

namespace hannk {

// Approximate log2(2^(x/2^log2_precision) +/- 1)*2^log2_precision_result
Expr approx_log2_exp2_plus_or_minus_one(Type type, Expr x, int sign, Expr log2_precision_x, int log2_precision_result) {
    const int log2_p = 15;
    int one = sign << log2_p;
    Expr one_plus_exp2_x = one + approx_exp2(x, log2_precision_x, log2_p);

    Expr raw = approx_log2(type, one_plus_exp2_x, log2_precision_result);

    // Since we computed log2(x*p) = log2(x) + log2(p), subtract log2(p) now.
    raw = raw - (log2_p << log2_precision_result);

    // For large x, the intermediate overflows. But log2(1 + 2^x) when x is large is just x.
    Expr threshold = 15 << log2_precision_x;
    Expr line = cast(type, rounding_shift_right(x, log2_precision_x - log2_precision_result));
    return select(x < threshold, raw, line);
}

Expr approx_log2p1_exp2(Type type, Expr x, Expr log2_precision_x, int log2_precision_result) {
    return approx_log2_exp2_plus_or_minus_one(type, x, 1, log2_precision_x, log2_precision_result);
}

Expr approx_log2m1_exp2(Type type, Expr x, Expr log2_precision_x, int log2_precision_result) {
    return approx_log2_exp2_plus_or_minus_one(type, x, -1, log2_precision_x, log2_precision_result);
}

class Logistic : public Generator<Logistic> {
public:
    Input<Buffer<uint8_t>> input_{"input", 1};

    Input<uint8_t> input_zero_{"input_zero"};
    // The input multiplier and shift should have an extra factor of -log2(e).
    Input<int16_t> input_multiplier_{"input_multiplier"};
    Input<uint16_t> input_shift_{"input_shift"};

    Output<Buffer<uint8_t>> output_{"output", 1};

    void generate() {
        // The algorithm.
        Var x("x");

        Expr input = (i16(input_(x)) - i16(input_zero_)) << 6;
        input = multiply_2x_high(input, input_multiplier_);

        //   256/(1 + 2^input)
        // = 256*2^(-log2(1 + 2^input))
        const int log2_precision = 8;
        Expr log2_inv_logistic = approx_log2p1_exp2(Int(16), input, input_shift_, log2_precision);
        Expr logistic = approx_exp2(Int(16), -log2_inv_logistic, log2_precision, 8);
        output_(x) = u8_sat(logistic);

        // Schedule.
        const int vector_size = natural_vector_size<uint8_t>();

        output_.vectorize(x, vector_size, TailStrategy::Predicate);
    }
};

class Tanh : public Generator<Tanh> {
public:
    Input<Buffer<uint8_t>> input_{"input", 1};

    Input<uint8_t> input_zero_{"input_zero"};
    // The input multiplier and shift should have an extra factor of 2*log2(e).
    Input<int16_t> input_multiplier_{"input_multiplier"};
    Input<uint16_t> input_shift_{"input_shift"};

    Output<Buffer<uint8_t>> output_{"output", 1};

    void generate() {
        // The algorithm.
        Var x("x");

        Expr input = (i16(input_(x)) - i16(input_zero_)) << 6;
        input = multiply_2x_high(input, input_multiplier_);

        // tanh(x) = (e^2x - 1)/(e^2x + 1). We baked a factor of 2*log2(e) into
        // the input multiplier and shift, so we just need to compute 2^x here.
        // TODO: It's probably better to just directly approximate tanh, but this
        // is simple and does not impact performance in any known cases.
        const int log2_precision = 8;
        Expr log2_n = approx_log2m1_exp2(Int(16), i16(abs(input)), input_shift_, log2_precision);
        Expr log2_d = approx_log2p1_exp2(Int(16), i16(abs(input)), input_shift_, log2_precision);
        Expr abs_output = approx_exp2(Int(16), log2_n - log2_d, log2_precision, 7);
        Expr output = select(input < 0, -abs_output, abs_output);
        output_(x) = u8_sat(output + 128);

        // Schedule.
        const int vector_size = natural_vector_size<uint8_t>();

        output_.vectorize(x, vector_size, TailStrategy::Predicate);
    }
};

}  // namespace hannk

HALIDE_REGISTER_GENERATOR(hannk::Logistic, Logistic)
HALIDE_REGISTER_GENERATOR(hannk::Tanh, Tanh)
