#include "Halide.h"
#include "common_halide.h"

using namespace Halide;
using namespace Halide::ConciseCasts;

namespace hannk {

class FullyConnected : public Generator<FullyConnected> {
public:
    Input<Buffer<uint8_t>> input_{"input", 2};
    Input<uint8_t> input_zero_{"input_zero"};

    Input<Buffer<uint8_t>> filter_{"filter", 2};
    Input<uint8_t> filter_zero_{"filter_zero"};

    Input<Buffer<int32_t>> bias_{"bias", 1};

    Input<uint8_t> output_zero_{"output_zero"};
    Input<int32_t> output_multiplier_{"output_multiplier"};
    Input<uint32_t> output_shift_{"output_shift"};
    Input<uint8_t> output_min_{"output_min"};
    Input<uint8_t> output_max_{"output_max"};

    Output<Buffer<>> output_{"output", 2};

    void generate() {
        Var c("c"), b("b");

        // We require the reduction dimension to be aligned to a uint8 vector.
        Expr filter_extent = align(filter_.dim(0).extent(), natural_vector_size<uint8_t>());
        filter_.dim(0).set_min(0);
        RDom rc(0, filter_extent);

        // We want to compute the reduction:
        // multiplied(c, b) = bias_(c)
        // multiplied(c, b) +=
        //    (i32(input) - i32(input_zero_)) *
        //    (i32(filter) - i32(filter_zero_))
        //
        // However, this precludes using efficient dot product instructions. To
        // fix this, expand the expression:
        //
        // multiplied(c, b) = bias_(c)
        // multiplied(c, b) +=
        //    i32(filter(rc, c)) * i32(input(rc, b)) -
        //    i32(filter(rc, c)) * i32(input_zero_) -
        //    i32(filter_zero_) * i32(input(rc, b)) +
        //    i32(filter_zero_) * i32(input_zero_)
        //
        // We can then separate this into several reductions. The last reduction
        // is a constant, and the middle two reductions can be computed once for
        // each c or b, instead of each (c, b).
        Func sum_filter("sum_filter");
        sum_filter(c) += u32(filter_(rc, c));

        Func sum_input("sum_input");
        sum_input(b) += u32(input_(rc, b));

        Func multiplied("multiplied");
        multiplied(c, b) =
            bias_(c) + filter_extent * filter_zero_ * input_zero_ -
            i32(sum_filter(c)) * input_zero_ -
            i32(sum_input(b)) * filter_zero_;

        multiplied(c, b) += i32(u16(input_(rc, b)) * u16(filter_(rc, c)));

        // Saturate and narrow the output.
        Expr output = multiply_2x_high(multiplied(c, b), output_multiplier_);
        output = i16_sat(rounding_shift_right(output, output_shift_));
        if (output_.type() == halide_type_of<uint8_t>()) {
            output = u8_sat(saturating_add(output, output_zero_));
            output = clamp(output, output_min_, output_max_);
        }
        output_(c, b) = output;

        // Schedule.
        // Reorder batches inside the outer loop over channels to improve locality
        // of accesses to the filter. This also allows us to compute the sum of the
        // filter for only a subset of channels at a time.
        Var co("co");
        Expr output_channels = output_.dim(0).extent();
        // Use half of the registers as accumulators.
        const int accum_registers = get_register_count(target) / 2;
        output_.compute_root()
            .specialize(output_channels >= accum_registers)
            .split(c, co, c, accum_registers, TailStrategy::ShiftInwards)
            .reorder(c, b, co)
            .vectorize(c);

        // Make a dummy outer loop if there aren't enough channels.
        output_
            .split(c, co, c, 1)
            .reorder(c, b, co);

        multiplied.compute_at(output_, b)
            .vectorize(c);
        // Enable sum_filter to be skipped if it isn't needed.
        multiplied.specialize(input_zero_ == 0);

        // The schedule here splits the reduction into 3 parts:
        // 1. The inner vector reduction factor (rci)
        // 2. The outer vector reduction factor (rc)
        // 3. The reduction of whole vectors (rco).
        // Step 2 is saved for the end, which is a total reduction of an int32 vector.
        // The other two steps map nicely to vector reductions like udot or pmaddwd.
        const int accum_vector_size = natural_vector_size<int32_t>();
        const int vector_reduction_factor = get_vector_reduction_factor(target, UInt(8));
        RVar rci, rco;
        multiplied.update()
            .split(rc, rc, rci, vector_reduction_factor)
            .split(rc, rco, rc, accum_vector_size);
        Func multiplied_intm = multiplied.update().rfactor(rc, co);

        multiplied_intm.compute_at(multiplied, b)
            .reorder_storage(co, c)
            .vectorize(co)
            .unroll(c)
            .update()
            .reorder(rci, co, c, rco)
            .unroll(c)
            .vectorize(co)
            .atomic()
            .vectorize(rci);

        // We could transpose here by adding a wrapper to multiplied_intm and reordering
        // the storage, which would enable the reduction below to be a pure vectorize
        // instead of a vector reduction, but this didn't seem to be better on either x86
        // or ARM.

        multiplied.update()
            .reorder(c, rc, b)
            .unroll(c)
            .atomic()
            .vectorize(rc);

        const int reduce_vector_size = natural_vector_size<uint8_t>();
        // This gets reused across batches.
        // I don't think it actually makes sense to unroll this, but if
        // we don't, LLVM does it for us but as an outer loop, which
        // is the worst of all possibilities.
        sum_filter.compute_at(output_, co)
            .vectorize(c)
            .update()
            .atomic()
            .reorder(c, rc)
            .unroll(c)
            .vectorize(rc, reduce_vector_size);

        sum_input.compute_root()
            .update()
            .atomic()
            .reorder(rc, b)
            .vectorize(rc, reduce_vector_size);
    }
};

class LstmElementwise : public Generator<LstmElementwise> {
public:
    Input<Buffer<int16_t>> input_gate_{"input_gate", 2};
    Input<Buffer<int16_t>> input_modulation_gate_{"input_modulation_gate", 2};
    Input<Buffer<int16_t>> forget_gate_{"forget_gate", 2};
    Input<Buffer<int16_t>> output_gate_{"output_gate", 2};
    Input<Buffer<int16_t>> prev_state_{"prev_state", 2};

    Func build() {
        Var c("c"), b("b");

        Expr input_gate_output = approx_logistic(15, input_gate_(c, b), 12, Int(16));
        Expr input_modulation_gate_output = approx_tanh(15, input_modulation_gate_(c, b), 12, Int(16));

        Expr forget_gate_output = approx_logistic(15, forget_gate_(c, b), 12, Int(16));
        Expr output_gate_output = approx_logistic(15, output_gate_(c, b), 12, Int(16));

        Expr input_times_input_modulation = multiply_2x_high(input_gate_output, input_modulation_gate_output);
        Expr prev_state_times_forget_state = multiply_2x_high(forget_gate_output, prev_state_(c, b));

        Expr new_state = rounding_shift_right(input_times_input_modulation, 4);
        new_state = saturating_add(new_state, prev_state_times_forget_state);

        Func output_int16("output_int16");
        output_int16(c, b) = multiply_2x_high(output_gate_output, approx_tanh(15, new_state, 11, Int(16)));

        Func output("output");
        Expr output_uint8 = u8_sat(rounding_shift_right(output_int16(c, b), 8) + 128);
        output(c, b) = {output_int16(c, b), output_uint8};

        output.compute_root()
            .vectorize(c, natural_vector_size<uint8_t>());

        output_int16.compute_at(output, c)
            .vectorize(c);

        return output;
    }
};

}  // namespace hannk

HALIDE_REGISTER_GENERATOR(hannk::FullyConnected, FullyConnected)
HALIDE_REGISTER_GENERATOR(hannk::LstmElementwise, LstmElementwise)
