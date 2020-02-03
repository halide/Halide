// 'AveragePool' implementation in Halide.

#include "common.h"
#include <Halide.h>

using Halide::Expr;
using Halide::Func;
using Halide::Generator;
using Halide::Var;
using Halide::BoundaryConditions::constant_exterior;
using Halide::ConciseCasts::u8_sat;

class AveragePool : public Generator<AveragePool> {
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

        // Add a zero boundary condition to x and y dimensions of the input.
        Func input_bounded = constant_exterior(input_, 0,
                                               {{Expr(), Expr()},
                                                {0, input_.dim(1).extent()},
                                                {0, input_.dim(2).extent()},
                                                {Expr(), Expr()}});

        // Shift the input spatially in [x, y] by -[pad_width, pad_height].
        Func shifted_input_bounded("shifted_input_bounded");
        shifted_input_bounded(depth, x, y, batch) =
            input_bounded(depth, x - pad_width_, y - pad_height_, batch);

        Func sum("sum");
        RDom filter_dom(0, filter_width_, 0, filter_height_);
        sum(depth, x, y, batch) += (cast<int32_t>(select(
            stride_ == 1,
            shifted_input_bounded(depth, x + filter_dom.x, y + filter_dom.y, batch),
            shifted_input_bounded(depth, x * stride_ + filter_dom.x,
                                  y * stride_ + filter_dom.y, batch))));

        Expr in_x_origin = x * stride_ - pad_width_;
        Expr x_start = max(0, -in_x_origin);
        Expr x_end = min(filter_width_, input_.dim(1).extent() - in_x_origin);

        Expr in_y_origin = y * stride_ - pad_height_;
        Expr y_start = max(0, -in_y_origin);
        Expr y_end = min(filter_height_, input_.dim(2).extent() - in_y_origin);

        Expr filter_count = (x_end - x_start) * (y_end - y_start);

        Func average("average");
        // We add filter_count / 2 before dividing by filter_count to round the
        // result.
        average(depth, x, y, batch) =
            (sum(depth, x, y, batch) + filter_count / 2) / filter_count;

        // Saturate and narrow the output.
        output_(depth, x, y, batch) =
            min(output_max_, max(output_min_, u8_sat(average(depth, x, y, batch))));

        bool use_hexagon =
            get_target().features_any_of({Target::HVX_64, Target::HVX_128});
        // Specifying .hexagon() on a Func will generate an RPC to run this stage
        // on Hexagon. If Hexagon is the host (that is, the architecture is
        // Hexagon), we have to omit the .hexagon() directive as we are already
        // running on Hexagon.
        if (use_hexagon && get_target().arch != Target::Hexagon) {
            output_.hexagon();
        }

        int vector_size_u8 = get_target().natural_vector_size<uint8_t>();
        if (get_target().has_feature(Target::HVX_64)) {
            vector_size_u8 = 64;
        } else if (get_target().has_feature(Target::HVX_128)) {
            vector_size_u8 = 128;
        }

        shifted_input_bounded.compute_at(output_, batch);

        // We only perform vectorization when the depth >= vector size.
        Expr can_vectorize_across_depth =
            output_.dim(0).extent() >= vector_size_u8;
        output_.specialize(can_vectorize_across_depth)
            .vectorize(depth, vector_size_u8);

        Var yi("yi");
        constexpr int kSplitFactor = 4;
        output_.split(y, y, yi, kSplitFactor).parallel(y);

        struct SpecialCase {
            int stride;
            int filter_width;
            int filter_height;
        };

        std::vector<SpecialCase> special_cases = {{1, 4, 4}, {2, 7, 7}};
        for (const SpecialCase &special_case : special_cases) {
            Expr params_matched = (filter_width_ == special_case.filter_width &&
                                   filter_height_ == special_case.filter_height &&
                                   stride_ == special_case.stride);
            sum.update(0).specialize(params_matched).unroll(filter_dom.x);
        }
    }
};

HALIDE_REGISTER_GENERATOR(AveragePool, AveragePool)
