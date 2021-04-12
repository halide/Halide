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

        Func inv_sqrt("inv_sqrt");
        const int log2_precision = 15;
        inv_sqrt(y) = approx_reciprocal_sqrt(sum_input_sq(y), log2_precision);

        // The output has a scale of 2^7 = 128 and offset of 128.
        Expr output = i32(input_zeroed(x, y)) * i32(inv_sqrt(y));
        output = i16_sat(rounding_shift_right(output, log2_precision - 7));
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
    }
};

}  // namespace hannk

HALIDE_REGISTER_GENERATOR(hannk::L2Normalization, L2Normalization)
