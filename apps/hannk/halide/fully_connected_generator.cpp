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
        Func multiplied("multiplied");
        Func multiplied_sums("multiplied_sums");
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

            // We actually do these two reductions together as a tuple. The first tuple
            // element is the positive terms, the second tuple element is the negative
            // terms. We separate them because doing a multiply subtract is hard on most
            // targets. It is marginally faster to move the filter * input_zero reduction
            // out of this loop, but it's a pain to implement well. This ends up not
            // being too bad on most targets, because we can only do 1 multiply-add per
            // load anyways. Doing an extra multiply-add for each load is ~free.
            // TODO: Often the filter is constant, and we could precompute the sum of
            // it elsewhere.
            multiplied_sums(c, b) = {
                bias_(c) + filter_extent * filter_zero_ * input_zero_,
                i32(sum_input(b)) * filter_zero_,
            };

            multiplied_sums(c, b) = {
                multiplied_sums(c, b)[0] + i32(u16(input_(rc, b)) * u16(filter_(rc, c))),
                multiplied_sums(c, b)[1] + i32(u16(filter_(rc, c)) * u16(input_zero_)),
            };

            // TODO: This subtract happens after the total vector reductions from the
            // above reduction. It would be a lot better if we could do this subtract
            // first somehow.
            multiplied(c, b) = multiplied_sums(c, b)[0] - multiplied_sums(c, b)[1];
        } else {
            multiplied(c, b) = bias_(c);
            multiplied(c, b) +=
                i32(i16(input_(rc, b)) - i16(input_zero_)) *
                i32(i16(filter_(rc, c)) - i16(filter_zero_));
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
        Var co("co");
        Expr output_channels = output_.dim(0).extent();
        // Use half of the registers as accumulators.
        const int accum_registers = 8;
        output_.compute_root()
            .specialize(output_channels >= accum_registers)
            .split(c, co, c, accum_registers, TailStrategy::ShiftInwards)
            .reorder(c, b, co)
            .vectorize(c);

        // Make a dummy outer loop if there aren't enough channels.
        output_
            .split(c, co, c, 1)
            .reorder(c, b, co);

        if (use_8bit_multiply(target)) {
            multiplied = multiplied_sums;
        }

        multiplied.compute_at(output_, b)
            .vectorize(c);
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

        multiplied_intm.compute_at(multiplied, b)
            .reorder_storage(co, c)
            .vectorize(co)
            .unroll(c)
            .update()
            .reorder(rci, co, c, rco)
            .unroll(c)
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
