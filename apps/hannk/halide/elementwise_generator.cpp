#include "Halide.h"
#include "halide/common_halide.h"
#include "halide/constants.h"
#include "interpreter/elementwise_program.h"

using namespace Halide;
using namespace Halide::ConciseCasts;

namespace hannk {

class Add : public Generator<Add> {
public:
    // Input buffers and quantization parameters.
    Input<Buffer<uint8_t, 2>> input1_{"input1"};
    Input<uint8_t> input1_zero_{"input1_zero"};
    Input<int16_t> input1_multiplier_{"input1_multiplier"};

    Input<Buffer<uint8_t, 2>> input2_{"input2"};
    Input<uint8_t> input2_zero_{"input2_zero"};
    Input<int16_t> input2_multiplier_{"input2_multiplier"};

    Input<uint8_t> output_zero_{"output_zero"};
    Input<uint8_t> output_min_{"output_min"};
    Input<uint8_t> output_max_{"output_max"};

    Output<Buffer<uint8_t, 2>> output_{"output"};

    void generate() {
        Var x("x"), y("y");

        Expr input1 = (i16(input1_(x, y)) - i16(input1_zero_)) << add_input_shift;
        Expr input2 = (i16(input2_(x, y)) - i16(input2_zero_)) << add_input_shift;

        input1 = widening_mul(input1, input1_multiplier_);
        input2 = widening_mul(input2, input2_multiplier_);
        Expr output = i16_sat(rounding_shift_right(input1 + input2, add_output_shift));

        output = u8_sat(saturating_add(output, output_zero_));
        output_(x, y) = clamp(output, output_min_, output_max_);

        // Schedule.
        const int vector_size = natural_vector_size<uint8_t>();

        output_.compute_root()
            .vectorize(x, vector_size * 2, TailStrategy::Predicate);

        // Support broadcasting in the c dimension for either input.
        input1_.dim(0).set_stride(Expr());
        input2_.dim(0).set_stride(Expr());
        // Most common case
        output_.specialize(input1_.dim(0).stride() == 1 && input2_.dim(0).stride() == 1);
        // Second most common case
        output_.specialize(input1_.dim(0).stride() == 1 && input2_.dim(0).stride() == 0);
        // Very uncommon (not seen in the wild)
        output_.specialize(input1_.dim(0).stride() == 0 && input2_.dim(0).stride() == 1);
        // Don't specialize for both strides being 0
        // output_.specialize(input1_.dim(0).stride() == 0 && input2_.dim(0).stride() == 0);
        output_.specialize_fail("input dimension 0 must have a stride of 0 or 1.");
    }
};

class Mul : public Generator<Mul> {
public:
    Input<Buffer<uint8_t, 2>> input1_{"input1"};
    Input<uint8_t> input1_zero_{"input1_zero"};

    Input<Buffer<uint8_t, 2>> input2_{"input2"};
    Input<uint8_t> input2_zero_{"input2_zero"};

    Input<uint8_t> output_zero_{"output_zero"};
    Input<int32_t> output_multiplier_{"output_multiplier"};
    Input<uint32_t> output_shift_{"output_shift"};
    Input<uint8_t> output_min_{"output_min"};
    Input<uint8_t> output_max_{"output_max"};

    Output<Buffer<uint8_t, 2>> output_{"output"};

    void generate() {
        Var x("x"), y("y");

        Expr input1 = (i16(input1_(x, y)) - i16(input1_zero_)) << mul_input_shift;
        Expr input2 = (i16(input2_(x, y)) - i16(input2_zero_)) << mul_input_shift;

        Expr output = multiply_2x_high(i32(input1) * i32(input2), output_multiplier_);
        output = i16_sat(rounding_shift_right(output, output_shift_));
        output = u8_sat(saturating_add(output, output_zero_));
        output_(x, y) = clamp(output, output_min_, output_max_);

        // Schedule.
        const int vector_size = natural_vector_size<uint8_t>();

        output_.compute_root()
            .vectorize(x, vector_size * 2, TailStrategy::Predicate);

        // Support broadcasting in the c dimension for either input.
        input1_.dim(0).set_stride(Expr());
        input2_.dim(0).set_stride(Expr());
        // Most common case
        output_.specialize(input1_.dim(0).stride() == 1 && input2_.dim(0).stride() == 1);
        // Second most common case
        output_.specialize(input1_.dim(0).stride() == 1 && input2_.dim(0).stride() == 0);
        // Very uncommon (not seen in the wild)
        output_.specialize(input1_.dim(0).stride() == 0 && input2_.dim(0).stride() == 1);
        // Don't specialize for both strides being 0
        // output_.specialize(input1_.dim(0).stride() == 0 && input2_.dim(0).stride() == 0);
        output_.specialize_fail("input dimension 0 must have a stride of 0 or 1.");
    }
};

// This is a generator that interprets programs to implement sequences of
// elementwise operations dynamically.
class Elementwise : public Generator<Elementwise> {
public:
    // This is the type used for all intermediate storage and computation.
    GeneratorParam<Type> intermediate_type_{"intermediate_type", Int(16)};

    // This is the type used for each output. The output of this pipeline
    // will be a 1D func that is a tuple of these types with non-zero bits.
    // Outputs are saturating casts of the intermediate type.
    GeneratorParam<Type> output1_type_{"output1_type", Int(0)};
    GeneratorParam<Type> output2_type_{"output2_type", Int(0)};
    GeneratorParam<Type> output3_type_{"output3_type", Int(0)};

    // An array of inputs.
    Input<Buffer<void, 2>[]> inputs_ { "inputs" };
    // The program to run. See elementwise_program.h for a description of
    // this buffer.
    Input<Buffer<int16_t, 2>> program_{"program"};

    // Type is determined by the GeneratorParams specified.
    Output<Buffer<void, 2>> output_{"output"};

    void generate() {
        Var x("x"), y("y"), u("u");

        Type intermediate_type = intermediate_type_;
        Type unsigned_intermediate = intermediate_type.with_code(halide_type_uint);
        const int q = intermediate_type.bits() - (intermediate_type.is_int() ? 1 : 0);

        Func scratch("scratch");
        scratch(x, y, u) = undef(intermediate_type_);

        // Load the inputs into the scratch memory.
        const int input_count = inputs_.size();
        for (int i = 0; i < input_count; i++) {
            scratch(x, y, -i - 1) = cast(intermediate_type, inputs_[i](x, y));
        }

        // scratch slot 0 is a constant 0.
        scratch(x, y, 0) = cast(intermediate_type, 0);

        RDom r(0, ElementwiseAssembler::OpCodeCount, 0, program_.dim(1).extent());
        Expr op = program_(0, r.y);
        Expr arg1 = program_(1, r.y);
        Expr arg2 = program_(2, r.y);
        Expr arg3 = cast(intermediate_type, program_(3, r.y));
        Expr arg4 = cast(intermediate_type, program_(4, r.y));

        Expr slot = r.y + 1;

        const int max_input = input_count - 1;
        Expr input1 = scratch(x, y, unsafe_promise_clamped(i32(arg1), -max_input - 1, slot));
        Expr input2 = scratch(x, y, unsafe_promise_clamped(i32(arg2), -max_input - 1, slot));

        std::vector<Expr> instructions = {
            scratch(x, y, slot),
            saturating_add(input1, input2 + arg3),
            saturating_sub(input1, input2 + arg3),
            saturating_add(multiply_2x_high(input1, input2 + arg3), arg4),
            rounding_mul_shift_right(input1, input2 + arg3, cast(unsigned_intermediate, arg4)),
            rounding_shift_right(input1, input2 + arg3),
            min(input1, input2 + arg3),
            max(input1, input2 + arg3),
            clamp(input1, arg3, arg4),
            rounding_shift_right(approx_logistic(q, input1, input2 + arg3, intermediate_type), q - arg4),
            rounding_shift_right(approx_tanh(q, input1, input2 + arg3, intermediate_type), q - arg4),
        };
        r.where(r.x == op);
        scratch(x, y, slot) = mux(r.x, instructions);

        std::vector<Type> output_types;
        if (((Type)output1_type_).bits() > 0) {
            output_types.push_back(output1_type_);
        }
        if (((Type)output2_type_).bits() > 0) {
            output_types.push_back(output2_type_);
        }
        if (((Type)output3_type_).bits() > 0) {
            output_types.push_back(output3_type_);
        }
        int output_count = output_types.size();

        // Grab the last output_count values from scratch and write them to each output.
        std::vector<Expr> outputs;
        for (int i = 0; i < output_count; i++) {
            Expr output_i = scratch(x, y, program_.dim(1).extent() - output_count + i + 1);
            output_i = saturating_cast(output_types[i], output_i);
            outputs.push_back(output_i);
        }
        output_(x, y) = Tuple(outputs);

        // Schedule.
        output_.compute_root()
            .vectorize(x, natural_vector_size<uint8_t>(), TailStrategy::Predicate);

        // Only allow this many instructions per input, so we can store scratch
        // on the real stack. This is a lame heuristic.
        const int max_instructions_per_input = 4;

        // Support broadcasting of dimension 0 of any input.
        for (int i = 0; i < input_count; i++) {
            inputs_[i].dim(0).set_stride(Expr());
            scratch.update(i).specialize(inputs_[i].dim(0).stride() == 1);
            scratch.update(i).specialize(inputs_[i].dim(0).stride() == 0);
            scratch.update(i).specialize_fail("Input dimension 0 must have stride 0 or 1.");
        }
        scratch.update(input_count).unscheduled();  // constant zero

        const int slots = input_count * max_instructions_per_input;
        scratch
            .bound_extent(u, input_count + slots + 1)
            .store_in(MemoryType::Register)
            .update(input_count + 1)
            .unroll(r.x);

        program_.dim(0).set_min(0).set_extent(ElementwiseAssembler::InstructionSize).set_stride(1);
        program_.dim(1).set_min(0).set_stride(ElementwiseAssembler::InstructionSize);
    }
};

}  // namespace hannk

HALIDE_REGISTER_GENERATOR(hannk::Add, Add)
HALIDE_REGISTER_GENERATOR(hannk::Mul, Mul)
HALIDE_REGISTER_GENERATOR(hannk::Elementwise, Elementwise)
