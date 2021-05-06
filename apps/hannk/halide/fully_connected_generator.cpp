#include "Halide.h"
#include "halide/common_halide.h"

using namespace Halide;
using namespace Halide::ConciseCasts;

namespace hannk {

// There are two codepaths in this generator. On targets with widening
// 8-bit multiplies, we implement the reduction by expanding the subtraction
// of the offsets into 4 reductions involving 8-bit multiplies. On targets
// without widening 8-bit multiplication, it's faster to just subtract the
// offsets and use 16-bit multiplications.
bool use_8bit_multiply(const Target &target) {
    return target.arch != Target::X86 || target.has_feature(Target::AVX512_SapphireRapids);
}

class FullyConnected : public Generator<FullyConnected> {
public:
    Input<Buffer<uint8_t>> input_{"input", 2};
    Input<uint8_t> input_zero_{"input_zero"};

    Input<Buffer<uint8_t>> filter_{"filter", 2};
    Input<uint8_t> filter_zero_{"filter_zero"};

    Input<Buffer<int32_t>> bias_{"bias", 1};

    Input<uint8_t> output_zero_{"output_zero"};
    Input<int32_t> output_multiplier_{"output_multiplier"};
    // TODO: We only need this to be a signed shift for exactly one known network.
    // Figure out if there is something else we should be doing instead.
    Input<int32_t> output_shift_{"output_shift"};
    Input<uint8_t> output_min_{"output_min"};
    Input<uint8_t> output_max_{"output_max"};

    Output<Buffer<>> output_{"output", 2};

    void generate() {
        Var c("c"), b("b");

        // We require the reduction dimension to be aligned to a uint8 vector.
        Expr filter_extent = align(filter_.dim(0).extent(), natural_vector_size<uint8_t>());
        filter_.dim(0).set_min(0);
        RDom rc(0, filter_extent);

        Func sum_input("sum_input");
        Func sum_filter("sum_filter");
        Func multiplied("multiplied");
        if (use_8bit_multiply(target)) {
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
            sum_input(b) += u32(input_(rc, b));
            sum_filter(c) += u32(filter_(rc, c));

            multiplied(c, b) =
                bias_(c) + filter_extent * filter_zero_ * input_zero_ -
                i32(sum_input(b)) * filter_zero_;

            multiplied(c, b) += i32(u16(filter_(rc, c)) * u16(input_(rc, b)));

            // TODO: This subtract happens after the total vector reductions from the
            // above reduction. It would be a lot better if we could do this subtract
            // first somehow.
            multiplied(c, b) = multiplied(c, b) - i32(sum_filter(c)) * i32(input_zero_);
        } else {
            multiplied(c, b) = bias_(c);
            multiplied(c, b) +=
                i32(i16(filter_(rc, c)) - i16(filter_zero_)) *
                i32(i16(input_(rc, b)) - i16(input_zero_));
        }

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
        Var co("co"), bo("bo");
        Expr output_channels = output_.dim(0).extent();
        Expr output_batches = output_.dim(1).extent();
        // Use half of the registers as accumulators.
        const int accum_registers = get_register_count(target) / 2;
        // When we have enough batches, compute a few of them at a time, so we can
        // re-use the filter a few times.
        const int tile_batches = 4;
        output_.compute_root()
            .specialize(output_channels >= accum_registers / 4 && output_batches >= tile_batches)
            .split(c, co, c, accum_registers / tile_batches, TailStrategy::ShiftInwards)
            .split(b, bo, b, tile_batches, TailStrategy::ShiftInwards)
            .reorder(c, b, bo, co)
            .vectorize(c)
            .unroll(b);

        // Handle the batch 1 case. In this case, we need an accumulator for both
        // the filter and the input.
        output_
            .specialize(output_channels >= accum_registers)
            .split(c, co, c, accum_registers / 2, TailStrategy::ShiftInwards)
            .split(b, bo, b, 1)
            .reorder(c, b, bo, co)
            .vectorize(c)
            .unroll(b);

        // Make dummy outer loops if there aren't enough channels or batches.
        output_
            .split(c, co, c, 1)
            .split(b, bo, b, 1)
            .reorder(c, b, bo, co);

        multiplied.compute_at(output_, bo)
            .vectorize(c)
            .unroll(b);
        // Enable sum_filter to be skipped if it isn't needed.
        multiplied.specialize(filter_zero_ == 0);

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

        multiplied_intm.compute_at(output_, bo)
            .reorder_storage(co, c)
            .vectorize(co)
            .unroll(c)
            .unroll(b)
            .update()
            .reorder(rci, co, c, b, rco)
            .unroll(c)
            .unroll(b)
            .vectorize(co)
            .atomic()
            .vectorize(rci)
            .specialize(input_zero_ == 0);

        // We could transpose here by adding a wrapper to multiplied_intm and reordering
        // the storage, which would enable the reduction below to be a pure vectorize
        // instead of a vector reduction, but this didn't seem to be better on either x86
        // or ARM.

        multiplied.update()
            .reorder(c, rc, b)
            .unroll(c)
            .atomic()
            .vectorize(rc);

        if (use_8bit_multiply(target)) {
            // We schedule this to use the same loops as multiplied_intm above, so we can
            // compute_with it.
            sum_filter.compute_at(output_, bo)
                .vectorize(c);
            sum_filter.update()
                .split(rc, rc, rci, vector_reduction_factor)
                .split(rc, rco, rc, accum_vector_size);
            Func sum_filter_intm = sum_filter.update().rfactor(rc, co);

            sum_filter_intm.compute_at(output_, bo)
                .reorder_storage(co, c)
                .vectorize(co)
                .unroll(c)
                .update()
                .reorder(rci, co, c, rco)
                .unroll(c)
                .vectorize(co)
                .atomic()
                .vectorize(rci);
            sum_filter_intm.update().compute_with(multiplied_intm.update(), rco);

            sum_filter.update()
                .reorder(c, rc)
                .unroll(c)
                .atomic()
                .vectorize(rc);

            multiplied.update(1)
                .vectorize(c)
                .unroll(b);

            // This reduction could be optimized better, but it rarely matters much.
            const int reduce_vector_size = natural_vector_size<uint8_t>();
            sum_input.compute_root()
                .update()
                .atomic()
                .reorder(rc, b)
                .vectorize(rc, reduce_vector_size);
        }
    }
};

}  // namespace hannk

HALIDE_REGISTER_GENERATOR(hannk::FullyConnected, FullyConnected)
