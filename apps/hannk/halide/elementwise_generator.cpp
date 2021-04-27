#include "Halide.h"
#include "common_halide.h"
#include "interpreter/elementwise_program.h"

using namespace Halide;
using namespace Halide::ConciseCasts;

namespace hannk {

class Add : public Generator<Add> {
public:
    // Input buffers and quantization parameters.
    Input<Buffer<uint8_t>> input1_{"input1", 2};
    Input<uint8_t> input1_zero_{"input1_zero"};
    Input<int32_t> input1_multiplier_{"input1_multiplier"};
    Input<uint32_t> input1_shift_{"input1_shift"};

    Input<Buffer<uint8_t>> input2_{"input2", 2};
    Input<uint8_t> input2_zero_{"input2_zero"};
    Input<int32_t> input2_multiplier_{"input2_multiplier"};
    Input<uint32_t> input2_shift_{"input2_shift"};

    // Offset, quantization multiplier and shift for the output.
    Input<uint8_t> output_zero_{"output_zero"};
    Input<int32_t> output_multiplier_{"output_multiplier"};
    Input<uint32_t> output_shift_{"output_shift"};
    Input<uint8_t> output_min_{"output_min"};
    Input<uint8_t> output_max_{"output_max"};

    Output<Buffer<uint8_t>> output_{"output", 2};

    void generate() {
        Var x("x"), y("y");

        Expr input1 = i32(i16(input1_(x, y)) - i16(input1_zero_)) << 20;
        Expr input2 = i32(i16(input2_(x, y)) - i16(input2_zero_)) << 20;

        input1 = rounding_shift_right(multiply_2x_high(input1, input1_multiplier_), input1_shift_);
        input2 = rounding_shift_right(multiply_2x_high(input2, input2_multiplier_), input2_shift_);

        Expr output = multiply_2x_high(input1 + input2, output_multiplier_);
        output = i16_sat(rounding_shift_right(output, output_shift_));
        output = u8_sat(saturating_add(output, output_zero_));
        output_(x, y) = clamp(output, output_min_, output_max_);

        // Schedule.
        const int vector_size = natural_vector_size<uint8_t>();

        output_.compute_root()
            .vectorize(x, vector_size, TailStrategy::Predicate);

        // Support broadcasting in the c dimension for input2.
        input2_.dim(0).set_stride(Expr());
        output_.specialize(input2_.dim(0).stride() == 1);
        output_.specialize(input2_.dim(0).stride() == 0);
        output_.specialize_fail("input2 dimension 0 must have a stride of 0 or 1.");
    }
};

class Mul : public Generator<Mul> {
public:
    Input<Buffer<uint8_t>> input1_{"input1", 2};
    Input<uint8_t> input1_zero_{"input1_zero"};

    Input<Buffer<uint8_t>> input2_{"input2", 2};
    Input<uint8_t> input2_zero_{"input2_zero"};

    Input<uint8_t> output_zero_{"output_zero"};
    Input<int32_t> output_multiplier_{"output_multiplier"};
    Input<uint32_t> output_shift_{"output_shift"};
    Input<uint8_t> output_min_{"output_min"};
    Input<uint8_t> output_max_{"output_max"};

    Output<Buffer<uint8_t>> output_{"output", 2};

    void generate() {
        Var x("x"), y("y");

        Expr input1 = (i16(input1_(x, y)) - i16(input1_zero_)) << 6;
        Expr input2 = (i16(input2_(x, y)) - i16(input2_zero_)) << 6;

        Expr output = multiply_2x_high(i32(input1) * i32(input2), output_multiplier_);
        output = i16_sat(rounding_shift_right(output, output_shift_));
        output = u8_sat(saturating_add(output, output_zero_));
        output_(x, y) = clamp(output, output_min_, output_max_);

        // Schedule.
        const int vector_size = natural_vector_size<uint8_t>();

        output_.compute_root()
            .vectorize(x, vector_size, TailStrategy::Predicate);

        // Support broadcasting in the c dimension for input2.
        input2_.dim(0).set_stride(Expr());
        output_.specialize(input2_.dim(0).stride() == 1);
        output_.specialize(input2_.dim(0).stride() == 0);
        output_.specialize_fail("input2 dimension 0 must have a stride of 0 or 1.");
    }
};

// This is a generator that interprets programs to implement sequences of
// elementwise operations dynamically.
class Elementwise : public Generator<Elementwise> {
public:
    GeneratorParam<Type> intermediate_type_{"intermediate_type", Int(16)};
    GeneratorParam<Type> output1_type_{"output1_type", Int(0)};
    GeneratorParam<Type> output2_type_{"output2_type", Int(0)};

    Input<Buffer<>[]> inputs_{"inputs", 1};
    Input<Buffer<int16_t>> program_{"program", 2};

    Func build() {
        Var x("x"), u("u");

        Type intermediate_type = intermediate_type_;
        Type unsigned_intermediate = intermediate_type.with_code(halide_type_uint);
        const int q = intermediate_type.bits() - (intermediate_type.is_int() ? 1 : 0);

        Func scratch("scratch");
        scratch(x, u) = undef(intermediate_type_);

        // Load the inputs into the scratch memory.
        const int input_count = inputs_.size();
        for (int i = 0; i < input_count; i++) {
            scratch(x, -i - 1) = cast(intermediate_type, inputs_[i](x));
        }
        // scratch slot 0 is a constant 0.
        scratch(x, 0) = cast(intermediate_type, 0);

        RDom r(0, ElementwiseProgram::OpCodeCount, 0, program_.dim(1).extent());
        Expr op = program_(0, r.y);
        Expr arg1 = program_(1, r.y);
        Expr arg2 = program_(2, r.y);
        Expr arg3 = cast(intermediate_type, program_(3, r.y));
        Expr arg4 = cast(intermediate_type, program_(4, r.y));

        const int max_input = input_count - 1;
        Expr input1 = scratch(x, unsafe_promise_clamped(i32(arg1), -max_input, r.y + 1));
        Expr input2 = scratch(x, unsafe_promise_clamped(i32(arg2), -max_input, r.y + 1));

        r.where(r.x == op);
        scratch(x, r.y + 1) = mux(r.x, {
            arg3,
            saturating_add(input1, input2 + arg3),
            saturating_sub(input1, input2 + arg3),
            rounding_mul_shift_right(input1, input2 + arg3, cast(unsigned_intermediate, arg4)),
            rounding_shift_right(input1, input2 + arg3),
            min(input1, input2 + arg3),
            max(input1, input2 + arg3),
            rounding_shift_right(approx_logistic(q, input1, input2 + arg3, intermediate_type), q - arg4),
            rounding_shift_right(approx_tanh(q, input1, input2 + arg3, intermediate_type), q - arg4),
        });

        Func output("output");
        std::vector<Type> output_types;
        if (((Type)output1_type_).bits() > 0) {
            output_types.push_back(output1_type_);
        }
        if (((Type)output2_type_).bits() > 0) {
            output_types.push_back(output2_type_);
        }
        int output_count = output_types.size();

        std::vector<Expr> outputs;
        for (int i = 0; i < output_count; i++) {
            outputs.push_back(saturating_cast(output_types[i], scratch(x, program_.dim(1).extent() - output_count + i + 1)));
        }
        output(x) = Tuple(outputs);

        // Schedule.
        output.compute_root()
            .vectorize(x, natural_vector_size<uint8_t>());

        // Only allow this many instructions per input, so we can store scratch
        // on the real stack.
        const int max_instructions_per_input = 4;

        scratch
            .bound_extent(u, input_count * (max_instructions_per_input + 1))
            .store_in(MemoryType::Register)
            .update(input_count + 1)
            .unroll(r.x);

        program_.dim(0).set_min(0).set_extent(5).set_stride(1);
        program_.dim(1).set_min(0).set_stride(5);

        return output;
    }
};

}  // namespace hannk

HALIDE_REGISTER_GENERATOR(hannk::Add, Add)
HALIDE_REGISTER_GENERATOR(hannk::Mul, Mul)
HALIDE_REGISTER_GENERATOR(hannk::Elementwise, Elementwise)
