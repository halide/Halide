#include "Halide.h"
#include "common_halide.h"

using namespace Halide;
using namespace Halide::ConciseCasts;

namespace hannk {

class Add : public Generator<Add> {
public:
    // Left shift for both inputs.
    Input<uint32_t> left_shift_{"left_shift"};

    // Input buffers.
    Input<Buffer<uint8_t>> input1_{"input1", 4};
    Input<Buffer<uint8_t>> input2_{"input2", 4};

    // Offset, quantization multiplier and shift for the left hand side.
    Input<uint8_t> input1_zero_{"input1_zero"};
    Input<int32_t> input1_multiplier_{"input1_multiplier"};
    Input<uint32_t> input1_shift_{"input1_shift"};

    // Offset, quantization multiplier and shift for the right hand side.
    Input<uint8_t> input2_zero_{"input2_zero"};
    Input<int32_t> input2_multiplier_{"input2_multiplier"};
    Input<uint32_t> input2_shift_{"input2_shift"};

    // Offset, quantization multiplier and shift for the output.
    Input<uint8_t> output_zero_{"output_zero"};
    Input<int32_t> output_multiplier_{"output_multiplier"};
    Input<uint32_t> output_shift_{"output_shift"};
    Input<uint8_t> output_min_{"output_min"};
    Input<uint8_t> output_max_{"output_max"};

    Output<Buffer<uint8_t>> output_{"output", 4};

    void generate() {
        Var c("c"), x("x"), y("y"), b("b");

        Expr input1 = input1_(c, x, y, b);
        Expr input2 = input2_(c, x, y, b);

        input1 = i32(i16(input1) - i16(input1_zero_)) << left_shift_;
        input2 = i32(i16(input2) - i16(input2_zero_)) << left_shift_;

        input1 = multiply_quantized(input1, input1_multiplier_, input1_shift_);
        input2 = multiply_quantized(input2, input2_multiplier_, input2_shift_);

        Expr output = multiply_quantized(input1 + input2, output_multiplier_, output_shift_);
        output = saturating_add(i16_sat(output), output_zero_);
        output_(c, x, y, b) = clamp(u8_sat(output), output_min_, output_max_);

        // Schedule.
        const int vector_size = natural_vector_size<uint8_t>();

        output_.compute_root()
            .vectorize(c, vector_size, TailStrategy::Predicate);

        // Support broadcasting in the c dimension for input2.
        input2_.dim(0).set_stride(Expr());
        output_.specialize(input2_.dim(0).stride() == 0);
        output_.specialize(input2_.dim(0).stride() == 1);
        output_.specialize_fail("inpu2 dimension 0 must have a stride of 0 or 1.");
    }
};

}  // namespace hannk

HALIDE_REGISTER_GENERATOR(hannk::Add, Add)
