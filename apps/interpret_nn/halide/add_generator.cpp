// 'Add4DUint8Uint8' operation generator with quantization on inputs and output.
// Inputs are expected as 4-dimensional arrays with possibly different sizes.
// Each input is replicated as needed along a dimension, so that both inputs
// have the same size before addition.

#include "Halide.h"
#include "common_halide.h"

using Halide::Generator;

namespace interpret_nn {

using Halide::ConciseCasts::i32;
using Halide::ConciseCasts::u32;
using Halide::ConciseCasts::u8_sat;

class Add : public Generator<Add> {
public:
    // Parameter ordering here is the same as in tfmini's interface for 'Add'.

    // Left shift for both inputs.
    Input<int32_t> left_shift_{"left_shift"};

    // Input images.
    Input<Buffer<uint8_t>> input1_{"input1", 4};
    Input<Buffer<uint8_t>> input2_{"input2", 4};

    // Offset, quantization multiplier and shift for the left hand side.
    Input<int32_t> input1_offset_{"input1_offset"};
    Input<int32_t> input1_multiplier_{"input1_multiplier"};
    Input<int32_t> input1_shift_{"input1_shift"};

    // Offset, quantization multiplier and shift for the right hand side.
    Input<int32_t> input2_offset_{"input2_offset"};
    Input<int32_t> input2_multiplier_{"input2_multiplier"};
    Input<int32_t> input2_shift_{"input2_shift"};

    // Offset, quantization multiplier and shift for the output.
    Input<int32_t> output_offset_{"output_offset"};
    Input<int32_t> output_multiplier_{"output_multiplier"};
    Input<int32_t> output_shift_{"output_shift"};

    Input<uint8_t> output_min_{"output_min"};
    Input<uint8_t> output_max_{"output_max"};

    Output<Buffer<uint8_t>> output_{"output", 4};

    void generate() {
        Var c("c"), x("x"), y("y"), b("b");

        Func shifted_input1_with_offset("shifted_input1_with_offset");
        shifted_input1_with_offset(c, x, y, b) =
            i32(input1_(c, x, y, b) + input1_offset_) *
            (1 << u32(left_shift_));

        Func shifted_input2_with_offset("shifted_input2_with_offset");
        shifted_input2_with_offset(c, x, y, b) =
            i32(input2_(c, x, y, b) + input2_offset_) *
            (1 << u32(left_shift_));

        Func scaled1("scaled1");
        scaled1(c, x, y, b) =
            MultiplyByQuantizedMultiplierSmallerThanOne(shifted_input1_with_offset(c, x, y, b),
                                                        input1_multiplier_, input1_shift_);

        Func scaled2("scaled2");
        scaled2(c, x, y, b) =
            MultiplyByQuantizedMultiplierSmallerThanOne(shifted_input2_with_offset(c, x, y, b),
                                                        input2_multiplier_, input2_shift_);

        Func sum("sum");
        sum(c, x, y, b) = scaled1(c, x, y, b) + scaled2(c, x, y, b);

        Func scaled_sum("scaled_sum");
        scaled_sum(c, x, y, b) =
            MultiplyByQuantizedMultiplierSmallerThanOne(sum(c, x, y, b), output_multiplier_,
                                                        output_shift_) +
            output_offset_;

        output_(c, x, y, b) =
            clamp(u8_sat(scaled_sum(c, x, y, b)), output_min_, output_max_);

        // Schedule.
        const int vector_size = natural_vector_size<uint8_t>();

        // Require that the operands are tensors, and that C and X have the same
        // extents.
        InterpretAsTensor(output_);
        InterpretAsTensor(input1_);
        InterpretAsTensor(input2_);
        RequireSameExtentCX(output_, input1_);
        RequireSameExtentCX(output_, input2_);

        // Fuse C and X if we can. If there is no broadcasting, we can usually do
        // this. This means we don't need to worry about the vector size dividing
        // the number of channels.
        Expr can_fuse_cx =
            CanFuseCX(output_) && CanFuseCX(input1_) && CanFuseCX(input2_);
        Var cx("cx");
        output_
            .specialize(can_fuse_cx)
            .fuse(c, x, cx)
            .vectorize(cx, vector_size, TailStrategy::ShiftInwards);

        // If not, just vectorize C.
        Expr output_channels = output_.dim(0).extent();
        output_
            .specialize(output_channels >= vector_size)
            .vectorize(c, vector_size, TailStrategy::ShiftInwards);
    }
};

}  // namespace interpret_nn

HALIDE_REGISTER_GENERATOR(interpret_nn::Add, Add)
