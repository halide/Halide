// This generator implements convolution and schedules for CPU and HVX.
//
// The pipeline implements the following operations:
// (1) an input offset is added to the 8-bit input
// (2) a filter offset is added to the 8-bit filter
// (3) perform convolution
// (4) convolution result is right-shifted and multiplied by a multiplier
// (5) an output offset is added to the quantized convolution result
// (6) the output is saturated and narrowed to 8-bit

// The output dimension is a function of input dimension, filter dimension,
// padding and stride.
// Input dimension: {input_depth, input_width, input_height, input_batches}
// Filter dimension: {filter_depth(=input_depth), filter_width, filter_height,
// filter_batches}
// Output dimension: {filter_batches, ceil((input_width + 2 * pad_width -
// filter_width) / stride) + 1, ceil((input_height + 2 * pad_height -
// filter_height) / stride) + 1, input_batches}

#include "common.h"
#include <Halide.h>

using Halide::Generator;
using Halide::Var;
using Halide::BoundaryConditions::constant_exterior;
using Halide::ConciseCasts::i16;
using Halide::ConciseCasts::u16_sat;
using Halide::ConciseCasts::u8_sat;

class Convolution : public Generator<Convolution> {
public:
    // Unsigned 8-bit input tensor, indexed by input_depth, input_x, input_y,
    // input_batch.
    Input<Buffer<uint8_t>> input_{"input", 4};

    // A 4D array of 8-bit filter coefficients indexed by filter_depth, filter_x,
    // filter_y, filter_batch (aka. output_depth).
    Input<Buffer<uint8_t>> filter_{"filter", 4};

    // A 1D array of 32-bit biases. The bias should be added to the depth
    // dimension of the output (i.e., # filter batches).
    Input<Buffer<int32_t>> bias_{"bias", 1};

    // Offsets and multipliers for the input, filter, and output.
    Input<int16_t> input_offset_{ "input_offset", 0, -255, 0 };
    Input<int16_t> filter_offset_{ "filter_offset", 0, -255, 0 };

    // For each x, y, batch, only the first input_depth_ elements can be non-zero.
    // All remaining elements are assigned byte_zero_. This value should be <=
    // input_.dim(0).extent()
    Input<int> input_depth_{ "input_depth" };

    // The stride specifies how the input [x, y] is sub-subsampled. For every
    // spatial location [x, y] in the output buffer, the input buffer is sampled
    // spatially at [x * stride, y * stride]. The caller is responsible for
    // allocating the correct output memory.
    Input<int> stride_{ "stride" };
    Input<int> pad_width_{ "pad_width" };
    Input<int> pad_height_{ "pad_height" };
    // byte_zero_ denotes the value padded at the input tensor boundary (in the x
    // and y dimensions). The name byte_zero_ follows tfmini convention.
    Input<uint8_t> byte_zero_{ "byte_zero" };

    // Parameters for pointwise operations on the output.
    Input<int> output_multiplier_{ "output_multiplier" };
    Input<int> output_shift_{ "output_shift" };
    Input<int> output_offset_{ "output_offset", 0, 0, 255 };
    Input<uint8_t> output_min_{ "output_min" };
    Input<uint8_t> output_max_{ "output_max" };

    Output<Buffer<uint8_t>> output_{"output", 4};

    void generate() {
        // The algorithm.

        // Some free variables, where x and y represent the spatial dimensions.
        Var x("x"), y("y"), depth("depth"), batch("batch");

        // For the input, add the offset and upcast to 16-bit.
        Func input_with_offset("input_with_offset");
        input_with_offset(depth, x, y, batch) =
            i16(input_(depth, x, y, batch)) + input_offset_;

        // Add a zero boundary condition to x and y dimensions of the input.
        Func input_with_offset_bounded =
            constant_exterior(input_with_offset, i16(byte_zero_),
                              { { Expr(), Expr() },
                                { 0, input_.dim(1).extent() },
                                { 0, input_.dim(2).extent() },
                                { Expr(), Expr() } });

        // For the filter, add the offset and upcast to 16-bit.
        Func filter_with_offset("filter_with_offset");
        filter_with_offset(depth, x, y, batch) =
            i16(filter_(depth, x, y, batch)) + filter_offset_;

        // Shift the input spatially in [x, y] by -[pad_width, pad_height].
        Func shifted_input_with_offset("shifted_input_with_offset");
        shifted_input_with_offset(depth, x, y, batch) = input_with_offset_bounded(
            depth, x - pad_width_, y - pad_height_, batch);

        // Do the convolution in 32-bit.
        Func convolved("convolved");
        RDom filter_dom(0, input_depth_, 0, filter_.dim(1).extent(), 0,
                        filter_.dim(2).extent());
        convolved(depth, x, y, batch) +=
            cast<int32_t>(filter_with_offset(filter_dom[0], filter_dom[1],
                                             filter_dom[2], depth)) *
            cast<int32_t>(shifted_input_with_offset(
                filter_dom[0], x * stride_ + filter_dom[1],
                y * stride_ + filter_dom[2], batch));

        Func scaled_plus_offset("scaled_plus_offset");
        scaled_plus_offset(depth, x, y, batch) =
            multiply_quantized_multiplier(
                convolved(depth, x, y, batch) + bias_(depth), output_multiplier_,
                output_shift_) +
            output_offset_;

        // Saturate and narrow the output.
        output_(depth, x, y, batch) =
            min(output_max_,
                max(output_min_,
                    u8_sat(u16_sat(scaled_plus_offset(depth, x, y, batch)))));

        const bool use_hexagon =
            get_target().features_any_of({ Target::HVX_64, Target::HVX_128 });

        // Specifying .hexagon() on a Func will generate an RPC to run this stage
        // on Hexagon. If Hexagon is the host (that is, the architecture is
        // Hexagon), we have to omit the .hexagon() directive as we are already
        // running on Hexagon.
        if (use_hexagon && get_target().arch != Target::Hexagon) {
            output_.hexagon();
        }

        // Schedule for CPU and HVX.
        int vector_size_u8 = get_target().natural_vector_size<uint8_t>();
        if (get_target().has_feature(Target::HVX_64)) {
            vector_size_u8 = 64;
        } else if (get_target().has_feature(Target::HVX_128)) {
            vector_size_u8 = 128;
        }
        // We only perform vectorization when the depth >= vector size.
        Expr can_vectorize_across_depth =
            filter_.dim(3).extent() >= vector_size_u8;

        output_.parallel(y)
            .specialize(can_vectorize_across_depth)
            .vectorize(depth, vector_size_u8);
        shifted_input_with_offset.compute_at(output_, batch);
    }
};

HALIDE_REGISTER_GENERATOR(Convolution, Convolution)
