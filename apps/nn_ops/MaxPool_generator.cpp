// 'MaxPool' implementation in Halide.

#include "common.h"
#include <Halide.h>

using Halide::Generator;
using Halide::BoundaryConditions::constant_exterior;
using Halide::ConciseCasts::u8_sat;

class MaxPool : public Generator<MaxPool> {
public:
    // Unsigned 8-bit input tensor, indexed by depth, x, y, batch.
    Input<Buffer<uint8_t>> input_{"input", 4};

    // The stride specifies how the input [x, y] are sub-subsampled. For every
    // spatial location [x, y] in the output buffer, the input buffer is sampled
    // spatially at [x * stride, y * stride]. The caller should ensure that
    // [x * stride, y * stride] is a valid spatial location in the input buffer.
    // Generally, this means setting the output buffer's [width, height] to be
    // the input buffer's [width, height] / stride.
    Input<int> stride_{"stride"};
    Input<int> pad_width_{"pad_width"};
    Input<int> pad_height_{"pad_height"};
    Input<int> filter_width_{"filter_width"};
    Input<int> filter_height_{"filter_height"};
    Input<uint8_t> output_min_{"output_min"};
    Input<uint8_t> output_max_{"output_max"};

    Output<Buffer<uint8_t>> output_{"output", 4};

    void generate() {
        // The algorithm.

        // Some free variables, where x and y represent the spatial dimensions.
        Var x("x"), y("y"), depth("depth"), batch("batch");

        // Cast the input to 32 bits.
        Func input_upcast("input_upcast");
        input_upcast(depth, x, y, batch) =
            cast<int32_t>(input_(depth, x, y, batch));

        // Use the minimum 32-bit integer for the boundary condition. Since
        // max(min_value, v) = v, this boundary condition enables us to implement
        // the local maximum operation using a reduction domain below and safely
        // handle boundary cases.
        constexpr int kMinValue = -2147483648;
        Func input_bounded = constant_exterior(input_upcast, kMinValue,
                                               {{Expr(), Expr()},
                                                {0, input_.dim(1).extent()},
                                                {0, input_.dim(2).extent()},
                                                {Expr(), Expr()}});

        // Shift the input spatially in [x, y] by -[pad_width, pad_height].
        Func shifted_input_bounded("shifted_input_bounded");
        shifted_input_bounded(depth, x, y, batch) =
            input_bounded(depth, x - pad_width_, y - pad_height_, batch);

        // Compute the local sliding-window maximum, where the window extents are
        // defined by the filter width and height.
        Func local_max("local_max");
        RDom filter_dom(0, filter_width_, 0, filter_height_);
        local_max(depth, x, y, batch) = maximum(select(
            stride_ == 1,
            shifted_input_bounded(depth, x + filter_dom.x, y + filter_dom.y, batch),
            shifted_input_bounded(depth, x * stride_ + filter_dom.x,
                                  y * stride_ + filter_dom.y, batch)));

        // Saturate and narrow the output.
        output_(depth, x, y, batch) =
            clamp(u8_sat(local_max(depth, x, y, batch)), output_min_, output_max_);

        // The schedule.

        const bool use_hexagon =
            get_target().features_any_of({Target::HVX_64, Target::HVX_128});

        if (use_hexagon) {
            output_.hexagon();
        }

        int vector_size_u8 = get_target().natural_vector_size<uint8_t>();
        if (get_target().has_feature(Target::HVX_64)) {
            vector_size_u8 = 64;
        } else if (get_target().has_feature(Target::HVX_128)) {
            vector_size_u8 = 128;
        }

        // We only perform vectorization when the depth >= vector size.
        Expr can_vectorize_across_depth =
            output_.dim(0).extent() >= vector_size_u8;
        output_.specialize(can_vectorize_across_depth)
            .vectorize(depth, vector_size_u8);

        // Parallelize across vertical strips.
        Var yi("yi");
        constexpr int kSplitFactor = 4;
        output_.split(y, y, yi, kSplitFactor).parallel(y);

        shifted_input_bounded.compute_at(output_, Var::outermost());
    }
};

HALIDE_REGISTER_GENERATOR(MaxPool, MaxPool)
