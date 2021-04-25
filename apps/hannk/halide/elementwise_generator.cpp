#include "Halide.h"
#include "common_halide.h"
#include "elementwise_program.h"

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

// TODO: These implementations are pretty slow, at least on x86. However:
// - They are readily implementable on every target
// - Produce identical results on every target
// - Avoid the use of lookup tables, which can be annoying on some targets
// - Negligibly impact overall performance in most realistic workloads

// Approximate log2(2^(x/2^q) +/- 1)*2^q
Expr approx_log2_exp2_plus_or_minus_one(int q, Expr x, int sign, Expr q_x, Type type = Int(32)) {
    const int q_exp = type.bits() / 2;
    int one = sign << q_exp;
    Expr raw = approx_log2(q, one + approx_exp2(q_exp, x, q_x, type), q_exp, type);

    // For large x, the intermediate overflows. But log2(1 + 2^x) when x is large is just x.
    Expr threshold = cast(x.type(), 16) << q_x;
    Expr line = cast(type, rounding_shift_right(x, q_x - q));
    return select(x < threshold, raw, line);
}

Expr approx_log2p1_exp2(int q, Expr x, Expr q_x, Type type = Int(32)) {
    return approx_log2_exp2_plus_or_minus_one(q, x, 1, q_x, type);
}

Expr approx_log2m1_exp2(int q, Expr x, Expr q_x, Type type = Int(32)) {
    return approx_log2_exp2_plus_or_minus_one(q, x, -1, q_x, type);
}

Expr logistic(Expr x, Expr q_x, Type type) {
    const int q = 8;
    Expr log2_d = approx_log2p1_exp2(q, x, q_x, Int(16));
    return approx_exp2(type.bits() - 1, -log2_d, q, type);
}

Expr tanh(Expr x, Expr q_x, Type type) {
    const int q = 8;
    Expr log2_n = approx_log2m1_exp2(q, i16(abs(x)), q_x, Int(16));
    Expr log2_d = approx_log2p1_exp2(q, i16(abs(x)), q_x, Int(16));
    Expr abs_output = approx_exp2(type.bits() - 1, log2_n - log2_d, q, type);
    return select(x < 0, -abs_output, abs_output);
}

// This is a generator that interprets programs to implement sequences of
// elementwise operations dynamically.
class Elementwise : public Generator<Elementwise> {
public:
    GeneratorParam<Type> intermediate_type_{"intermediate_type", Int(16)};

    Input<Buffer<>[]> inputs_{"inputs", 1};
    Input<Buffer<int32_t>> program_{"program", 2};

    Output<Buffer<>> output_{"output", 1};

    void generate() {
        Var x("x"), u("u");

        Type unsigned_intermediate = ((Type)intermediate_type_).with_code(halide_type_uint);

        Func scratch("scratch");
        scratch(x, u) = undef(intermediate_type_);

        // Load the inputs into the scratch memory.
        const int input_count = inputs_.size();
        for (int i = 0; i < input_count; i++) {
            scratch(x, -i - 1) = cast(intermediate_type_, inputs_[i](x));
        }
        // scratch slot 0 is a constant 0.
        scratch(x, 0) = cast(intermediate_type_, 0);

        RDom r(0, ElementwiseInstruction::OpCodeCount, 0, program_.dim(1).extent());
        Expr op = program_(0, r.y);
        Expr arg1 = program_(1, r.y);
        Expr arg2 = program_(2, r.y);
        Expr arg3 = cast(intermediate_type_, program_(3, r.y));

        const int max_input = input_count - 1;
        Expr input1 = scratch(x, unsafe_promise_clamped(arg1, -max_input, r.y + 1));
        Expr input2 = scratch(x, unsafe_promise_clamped(arg2, -max_input, r.y + 1));

        r.where(r.x == op);
        scratch(x, r.y + 1) = mux(r.x, {
            arg3,
            saturating_add(input1, input2 + arg3),
            saturating_sub(input1, input2 + arg3),
            rounding_mul_shift_right(input1, input2, cast(unsigned_intermediate, arg3)),
            rounding_shift_right(input1, input2 + arg3),
            min(input1, input2 + arg3),
            max(input1, input2 + arg3),
            rounding_shift_right(logistic(input1, input2, intermediate_type_), arg3),
            rounding_shift_right(tanh(input1, input2, intermediate_type_), arg3),
        });

        output_(x) = saturating_cast(output_.type(), scratch(x, program_.dim(1).extent()));

        // Schedule.
        output_.compute_root()
            .vectorize(x, natural_vector_size<uint8_t>(), TailStrategy::Predicate);

        // Only allow this many instructions per input, so we can store scratch
        // on the real stack.
        const int max_instructions_per_input = 4;

        scratch
            .bound_extent(u, input_count * (max_instructions_per_input + 1))
            .store_in(MemoryType::Register)
            .update(input_count + 1)
            .unroll(r.x);

        program_.dim(0).set_min(0).set_extent(4).set_stride(1);
        program_.dim(1).set_min(0).set_stride(4);
    }
};

}  // namespace hannk

HALIDE_REGISTER_GENERATOR(hannk::Add, Add)
HALIDE_REGISTER_GENERATOR(hannk::Mul, Mul)
HALIDE_REGISTER_GENERATOR(hannk::Elementwise, Elementwise)
