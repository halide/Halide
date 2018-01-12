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
    ImageParam input_{ UInt(8), 4, "input" };
    Param<int> stride_{ "stride" };
    Param<int> pad_width_{ "pad_width" };
    Param<int> pad_height_{ "pad_height" };
    Param<int> filter_width_{ "filter_width" };
    Param<int> filter_height_{ "filter_height" };
    // byte_zero_ denotes the value padded at the input tensor boundary (in the x
    // and y dimensions).
    Param<uint8_t> byte_zero_{ "byte_zero" };

    Func build() {
        Expr input_depth = input_.dim(0).extent();

        // The algorithm.

        // Some free variables, where x and y represent the spatial dimensions.
        Var d("d"), x("x"), y("y"), b("b");

        // Add a constant byte_zero as the boundary condition of the input.
        Func input_padded("input_padded");
        input_padded =
            constant_exterior(input_, byte_zero_, { { Expr(), Expr() }, { 0, input_.dim(1).extent() }, { 0, input_.dim(2).extent() }, { Expr(), Expr() } });

        Expr x_ungated_start = x * stride_ - pad_width_;
        Expr y_ungated_start = y * stride_ - pad_height_;
        Expr element_location = d / input_depth;
        Expr x_offset = element_location % filter_width_;
        Expr y_offset = element_location / filter_width_;

        Func output("output");
        output(d, x, y, b) =
            input_padded(d % input_depth, x_ungated_start + x_offset,
                         y_ungated_start + y_offset, b);

        // The schedule.

        int vector_size_u8 = natural_vector_size_with_hexagon(get_target());

        const bool use_hexagon =
            get_target().features_any_of({ Target::HVX_64, Target::HVX_128 });
        if (use_hexagon) {
            output.hexagon();
        }

        Var yo("yo"), yi("yi"), tile_index("tile_index");
        output.split(y, yo, yi, 2, TailStrategy::GuardWithIf)
            .fuse(x, yo, tile_index)
            .reorder(d, tile_index, b, yi)
            .vectorize(tile_index, vector_size_u8, TailStrategy::GuardWithIf)
            .parallel(yi);

        return output;
    }
};

HALIDE_REGISTER_GENERATOR(Im2col, Im2col)
