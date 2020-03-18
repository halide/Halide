// This generator implements depthwise convolution and schedules for HVX.
//
// The pipeline implements the following operations:
// (1) an input offset is added to the 8-bit input
// (2) a filter offset is added to the 8-bit filter
// (3) the offset input is resampled with a depth multiplier and stride
// (4) separable convolution between offset, resampled input and offset filter
// (5) convolution result is right-shifted and multiplied by a multiplier
// (6) an output offset is added to the quantized convolution result
// (7) the output is transformed by an activation function
// (8) the output is saturated and narrowed to 8-bit

// The Halide schedule for DepthwiseConv assumes the input depth to be a
// multiple of vector_size.

#include "common.h"
#include <Halide.h>

using Halide::Expr;
using Halide::Func;
using Halide::Generator;
using Halide::RDom;
using Halide::TailStrategy;
using Halide::Type;
using Halide::Var;
using Halide::BoundaryConditions::constant_exterior;
using Halide::ConciseCasts::u16_sat;
using Halide::ConciseCasts::u8_sat;

// Schedules the resampled input to be computed at the output, considering
// the natural vector size, the depth multiplier, and the filter dimensions.
void ScheduleResampledInput(const Func &output,
                            const Var &depth, const Var &y,
                            int depth_multiplier, int vector_size_u8,
                            Func *resampled_input) {
    // Compute the resampled input as needed for every row of the output.
    const int multiplied_vector_size = vector_size_u8 * depth_multiplier;
    resampled_input->compute_at(output, y);
    resampled_input->vectorize(depth, multiplied_vector_size,
                               TailStrategy::RoundUp);
}

// Specialize for particular filter sizes and input strides, which enables the
// compiler to optimize for these specific cases.
template<typename InputBuffer, typename OutputBuffer>
void SpecializeForFilterSizeAndInputStride(const RDom &filter_dom,
                                           const Expr &stride,
                                           InputBuffer *filter,
                                           OutputBuffer *output,
                                           Func *convolved) {
    if (filter) {
        filter->dim(1).set_min(0).dim(2).set_min(0);
    }
    std::vector<std::pair<int, int>> filter_sizes = {{3, 3}, {5, 5}};
    for (int other_stride : {1, 2}) {
        for (const std::pair<int, int> &filter_size : filter_sizes) {
            Expr params_matched = (filter->dim(1).extent() == filter_size.first &&
                                   filter->dim(2).extent() == filter_size.second &&
                                   stride == other_stride);
            if (output) {
                output->specialize(params_matched);
            }
            if (convolved) {
                convolved->update()
                    .specialize(params_matched)
                    .unroll(filter_dom.x)
                    .unroll(filter_dom.y);
            }
        }
    }
}

class DepthwiseConvolution : public Generator<DepthwiseConvolution> {
public:
    // The depth multiplier specifies the ratio between the output depth and the
    // input depth.
    GeneratorParam<int> depth_multiplier_{"depth_multiplier", 1, 1, 8};

    // Unsigned 8-bit input tensor, indexed by depth, x, y, batch.
    Input<Buffer<uint8_t>> input_{"input", 4};

    // A 3D array of 8-bit filter coefficients indexed by depth, x, y.
    Input<Buffer<uint8_t>> filter_{"filter", 3};

    // A 1D array of 32-bit biases indexed by depth.
    Input<Buffer<int32_t>> bias_{"bias", 1};

    // Offsets and multipliers for the input, filter, and output.
    Input<int16_t> input_offset_{"input_offset", 0, -255, 0};
    Input<int16_t> filter_offset_{"filter_offset", 0, -255, 0};
    Input<int> output_multiplier_{"output_multiplier"};
    Input<int> output_shift_{"output_shift"};
    Input<int> output_offset_{"output_offset", 0, 0, 255};
    // The stride specifies how the input [x, y] are sub-subsampled. For every
    // spatial location [x, y] in the output buffer, the input buffer is sampled
    // spatially at [x * stride, y * stride]. The caller should ensure that
    // [x * stride, y * stride] is a valid spatial location in the input buffer.
    // Generally, this means setting the output buffer's [width, height] to be
    // the input buffer's [width, height] / stride.
    Input<int> stride_{"stride", 1, 1, 2};
    Input<int> pad_width_{"pad_width"};
    Input<int> pad_height_{"pad_height"};
    Input<uint8_t> output_min_{"output_min"};
    Input<uint8_t> output_max_{"output_max"};

    Output<Buffer<uint8_t>> output_{"output", 4};

    void generate() {
        // The algorithm.

        // Some free variables, where x and y represent the spatial dimensions.
        Var x("x"), y("y"), depth("depth"), batch("batch");

        // Pad x and y with the value that produces zero after the input offset is
        // added. The input offset is bounded to the range of a uint8, so this is
        // safe.
        Func input_bounded =
            constant_exterior(input_, cast<uint8_t>(-input_offset_),
                              {{Expr(), Expr()},
                               {0, input_.dim(1).extent()},
                               {0, input_.dim(2).extent()},
                               {Expr(), Expr()}});

        // For the filter, add the offset and upcast to 16-bit.
        Func filter_with_offset("filter_with_offset");
        filter_with_offset(depth, x, y) =
            cast<int16_t>(filter_(depth, x, y)) + filter_offset_;

        // Shift the input spatially in [x, y] by -[pad_width, pad_height].
        Func shifted_input_with_offset("shifted_input_with_offset");
        shifted_input_with_offset(depth, x, y, batch) = input_bounded(
            depth, x - pad_width_, y - pad_height_, batch);

        // Apply the depth multiplier.
        Func resampled_input("resampled_input");
        resampled_input(depth, x, y, batch) =
            shifted_input_with_offset(depth / depth_multiplier_, x, y, batch);

        // For the input, add the offset and upcast to 16-bit. This happens after
        // resampling so we don't need to store/load as much data in the inner loop
        // (at the cost of one add in the inner loop instead).
        Func resampled_input_with_offset("resampled_input_with_offset");
        resampled_input_with_offset(depth, x, y, batch) =
            cast<int16_t>(resampled_input(depth, x, y, batch)) + input_offset_;

        // Do the convolution in 32-bit. Apply the input stride. As before, the
        // case stride == 1 is written separately for performance reasons.
        Func convolved("convolved");
        RDom filter_dom(0, filter_.dim(1).extent(), 0, filter_.dim(2).extent());
        convolved(depth, x, y, batch) +=
            (cast<int32_t>(filter_with_offset(depth, filter_dom.x, filter_dom.y)) *
             cast<int32_t>(
                 resampled_input_with_offset(depth, x * stride_ + filter_dom.x,
                                             y * stride_ + filter_dom.y, batch)));

        Func scaled_plus_offset("scaled_plus_offset");
        scaled_plus_offset(depth, x, y, batch) =
            multiply_quantized_multiplier(
                convolved(depth, x, y, batch) + bias_(depth), output_multiplier_,
                output_shift_) +
            output_offset_;

        // Saturate and narrow the output.
        output_(depth, x, y, batch) =
            clamp(u8_sat(scaled_plus_offset(depth, x, y, batch)),
                  output_min_, output_max_);

        // The schedule.
        int vector_size_u8 = get_target().natural_vector_size<uint8_t>();
        if (get_target().has_feature(Target::HVX_64)) {
            vector_size_u8 = 64;
        } else if (get_target().has_feature(Target::HVX_128)) {
            vector_size_u8 = 128;
        }
        const bool use_hexagon =
            get_target().features_any_of({Target::HVX_64, Target::HVX_128});

        // Specifying .hexagon() on a Func will generate an RPC to run this stage
        // on Hexagon. If Hexagon is the host (that is, the architecture is
        // Hexagon), we have to omit the .hexagon() directive as we are already
        // running on Hexagon.
        if (use_hexagon && get_target().arch != Target::Hexagon) {
            output_.hexagon();
        }

        output_.compute_root();

        // We can't parallize batches, as we often have just a single batch to
        // process. Also, x and y dimensions are often fairly small (8x8, 16x16).
        // For now, we parallize along y, but may need to adapt when benchmarking
        // real models.
        Var yi("yi");
        // For small tensors, make sure the split factor is not larger than the
        // output y extent.
        Expr y_split_factor = min(input_.dim(2).extent() / stride_, 4);

        output_.split(y, y, yi, y_split_factor).parallel(y);
        output_.vectorize(depth, vector_size_u8, TailStrategy::RoundUp);

        if (use_hexagon) {
            // Scheduling specifics for Hexagon.

            if (depth_multiplier_ > 1) {
                ScheduleResampledInput(output_, depth, y, depth_multiplier_,
                                       vector_size_u8, &resampled_input);
            }
            output_.prefetch(input_, yi);
        } else {
            // Scheduling specifics for CPU.

            // Special care has to be taken when the input depth is a multiple of 3,
            // because Halide specializes for this case (i.e., RGB color channels), or
            // the following Halide deinterleave compiler error will be encountered:
            // Internal error at third_party/halide/halide/src/Deinterleave.cpp:356
            // Condition failed: e.type().lanes() % 3 == 0
            if (depth_multiplier_ == 3) {
                ScheduleResampledInput(output_, depth, yi, depth_multiplier_,
                                       vector_size_u8, &resampled_input);
            }
        }
        SpecializeForFilterSizeAndInputStride(filter_dom, stride_, &filter_,
                                              &output_, &convolved);
    }
};

HALIDE_REGISTER_GENERATOR(DepthwiseConvolution,
                          DepthwiseConvolution)
