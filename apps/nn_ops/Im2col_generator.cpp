// This generator implements Im2col operation, which is a pre-processing step
// before ConvAsGemm. It allows convolution to be carried out as a matrix
// multiplication by filter x column.
//
// The Im2col operation performs a "sliding window" operation on input tensor,
// where the "sliding window" is parameterized by filter size and stride. For
// each window, the elements are stored along the depth of the output tensor.

#include "common.h"
#include <Halide.h>

using Halide::Generator;
using Halide::BoundaryConditions::constant_exterior;

class Im2col : public Generator<Im2col> {
public:
    // Unsigned 8-bit input tensor, indexed by depth, x, y, batch.
    Input<Buffer<uint8_t>> input_{"input", 4};
    Input<int> stride_{"stride"};
    Input<int> pad_width_{"pad_width"};
    Input<int> pad_height_{"pad_height"};
    Input<int> filter_width_{"filter_width"};
    Input<int> filter_height_{"filter_height"};
    // byte_zero_ denotes the value padded at the input tensor boundary (in the x
    // and y dimensions).
    Input<uint8_t> byte_zero_{"byte_zero"};

    Output<Buffer<uint8_t>> output_{"output", 4};

    void generate() {
        Expr input_depth = input_.dim(0).extent();

        // The algorithm.

        // Some free variables, where x and y represent the spatial dimensions.
        Var d("d"), x("x"), y("y"), b("b");

        // Add a constant byte_zero as the boundary condition of the input.
        Func input_padded("input_padded");
        input_padded =
            constant_exterior(input_, byte_zero_, {{Expr(), Expr()}, {0, input_.dim(1).extent()}, {0, input_.dim(2).extent()}, {Expr(), Expr()}});

        Expr x_ungated_start = x * stride_ - pad_width_;
        Expr y_ungated_start = y * stride_ - pad_height_;
        Expr element_location = d / input_depth;
        Expr x_offset = element_location % filter_width_;
        Expr y_offset = element_location / filter_width_;

        output_(d, x, y, b) =
            input_padded(d % input_depth, x_ungated_start + x_offset,
                         y_ungated_start + y_offset, b);

        // The schedule.
        int vector_size_u8 = get_target().natural_vector_size<uint8_t>();
        if (get_target().has_feature(Target::HVX_64)) {
            vector_size_u8 = 64;
        } else if (get_target().has_feature(Target::HVX_128)) {
            vector_size_u8 = 128;
        }

        const bool use_hexagon =
            get_target().features_any_of({Target::HVX_64, Target::HVX_128});
        if (use_hexagon) {
            output_.hexagon();
        }

        Var yo("yo"), yi("yi"), tile_index("tile_index");
        output_.split(y, yo, yi, 2, TailStrategy::GuardWithIf)
            .fuse(x, yo, tile_index)
            .reorder(d, tile_index, b, yi)
            .vectorize(tile_index, vector_size_u8, TailStrategy::GuardWithIf)
            .parallel(yi);
    }
};

HALIDE_REGISTER_GENERATOR(Im2col, Im2col)
