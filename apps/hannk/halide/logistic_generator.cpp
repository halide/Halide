#include "Halide.h"
#include "common_halide.h"

using namespace Halide;
using namespace Halide::ConciseCasts;

namespace hannk {

class Logistic : public Generator<Logistic> {
public:
    Input<Buffer<uint8_t>> input_{"input", 1};

    Input<uint32_t> left_shift_{"left_shift"};
    Input<uint8_t> input_zero_{"input_zero"};
    Input<int32_t> input_multiplier_{"input_multiplier"};

    Input<int32_t> input_range_radius_{"input_range_radius"};

    Output<Buffer<uint8_t>> output_{"output", 1};

    void generate() {
        // The algorithm.
        Var x("x");

        Expr input = i16(input_(x)) - i16(input_zero_);
        input = clamp(input, -input_range_radius_, input_range_radius_);
        input = i32(input) << left_shift_;
        input = multiply_2x_high(input, input_multiplier_);

        Expr logistic = 256.0f / (1.0f + exp(-cast<float>(input)));

        Func output("output");
        output(x) = u8_sat(logistic);

        // Schedule.
        // TODO: This schedule has very little ILP, but the extent of y
        // is often 1.
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
    }
};

}  // namespace hannk

HALIDE_REGISTER_GENERATOR(hannk::Logistic, Logistic)
