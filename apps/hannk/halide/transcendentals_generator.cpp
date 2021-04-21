#include "Halide.h"
#include "common_halide.h"

using namespace Halide;
using namespace Halide::ConciseCasts;

namespace hannk {

// Approximate log2(2^(x/2^q) +/- 1)*2^q
Expr approx_log2_exp2_plus_or_minus_one(int q, Expr x, int sign, Expr q_x, Type type = Int(32)) {
    const int q_exp = 15;
    int one = sign << q_exp;
    Expr raw = approx_log2(q, one + approx_exp2(q_exp, x, q_x), q_exp, type);

    // For large x, the intermediate overflows. But log2(1 + 2^x) when x is large is just x.
    Expr threshold = 16 << q_x;
    Expr line = cast(type, rounding_shift_right(x, q_x - q));
    return select(x < threshold, raw, line);
}

Expr approx_log2p1_exp2(int q, Expr x, Expr q_x, Type type = Int(32)) {
    return approx_log2_exp2_plus_or_minus_one(q, x, 1, q_x, type);
}

Expr approx_log2m1_exp2(int q, Expr x, Expr q_x, Type type = Int(32)) {
    return approx_log2_exp2_plus_or_minus_one(q, x, -1, q_x, type);
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
        const int q = 8;
        Expr log2_d = approx_log2p1_exp2(q, input, input_shift_, Int(16));
        Expr output = approx_exp2(8, -log2_d, q, Int(16));
        output_(x) = u8_sat(output);

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
        const int q = 8;
        Expr log2_n = approx_log2m1_exp2(q, i16(abs(input)), input_shift_, Int(16));
        Expr log2_d = approx_log2p1_exp2(q, i16(abs(input)), input_shift_, Int(16));
        Expr abs_output = approx_exp2(7, log2_n - log2_d, q, Int(16));
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
