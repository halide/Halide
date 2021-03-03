#include "Halide.h"
#include "common_halide.h"

using namespace Halide;
using namespace Halide::BoundaryConditions;
using namespace Halide::ConciseCasts;

namespace interpret_nn {

// Require that the first element of the innermost dimension is aligned to the
// given alignment, as measured in the number of elements of the buffer. This
// assumes that the dense dimension is dimension 0 (the default in Halide).
inline void RequireAlignedRows(Halide::OutputImageParam param, int alignment) {
    // The first dimension should have a min/extent aligned to the required
    // alignment, we assume the stride is 1.
    param.dim(0).set_min((param.dim(0).min() / alignment) * alignment);
    param.dim(0).set_extent((param.dim(0).extent() / alignment) * alignment);

    // The rest of the dimensions should have a stride aligned to the required
    // alignment.
    for (int i = 1; i < param.dimensions(); i++) {
        param.dim(i).set_stride((param.dim(i).stride() / alignment) * alignment);
    }
}

class Convolution : public Generator<Convolution> {
public:
    // Input(c, y, x)
    Input<Buffer<int8_t>> input_{"input_", 3};
    // Filter(n, c, y, x)
    Input<Buffer<int8_t>> filter_{"filter_", 4};
    // Output(n, y, x)
    Output<Buffer<int8_t>> output_{"output_", 3};

    void generate() {

        // Dimensions of the inner core matrix multiplication:
        //
        // Input[y][c] * Filter[c][n] = Output[y][n]
        //
        // y - outer loop dimension, must be aligned with accumulator count
        // c - inner loop dimension, must be aligned with vector_reduction
        // n - vectorized dimension, must be aligned with vector width
        //
        // x - additional input/output dimension
        // k.x, k.y - additional filter dimensions

        int vector_width = 64;  // (64 for Q7, 128 for Q8)

        // MAC input vector lane count
        int vector_reduction = 4;  // Q[uad]MAC instruction

        // MAC output accumulator register count
        int accumulator_count = 4;  // Wide Vector Registers

        // N partition output depth
        int np_size = vector_width / 1;  // reduces if using partitioned QMAC

        // C partition input/filter depth
        // (controls number of QMAC unrolled in inner loop)
        int cp_size = 16 * vector_reduction;

        Var n("n"), no("no"), ni("ni"), c("c"), x("x"), y("y"), yi("yi"), yo("yo");

        filter_.dim(1).set_min(0);
        filter_.dim(2).set_min(0);
        filter_.dim(3).set_min(0);
        Expr filter_c = filter_.dim(1).extent();
        Expr filter_y = filter_.dim(2).extent();
        Expr filter_x = filter_.dim(3).extent();

        // C is the inner matrix multiplication dimension that is eliminated
        // Align it so inner computation can be unrolled to a fix number
        filter_c = ((filter_c + cp_size - 1) / cp_size) * cp_size;
        RDom k(0, filter_x, 0, filter_y, 0, filter_c);  // k.z = c dimension
        std::cout << "[qys] " << filter_x << " " << filter_y << " " << filter_c << "\n";
        RVar co("co"), ci("ci"), cio("cio"), cii("cii");

        Func convolved("convolved");
        convolved(n, y, x) = cast(Int(24), 0);
        // x, k.x, k.y are additional dimensions
        convolved(n, y, x) += cast(Int(24), input_(k.z, y + k.y, x + k.x)) *
                              cast(Int(24), filter_(n, k.z, k.y, k.x));
        output_(n, y, x) = cast(Int(8), convolved(n, y, x) >> 6);

        // Schedule
        output_
            .split(n, no, ni, np_size, TailStrategy::RoundUp)
            .split(y, yo, yi, accumulator_count, TailStrategy::ShiftInwards)  // 4xQMAC
            .reorder(ni, yi, yo, x, no)
            .vectorize(ni, np_size)
            .unroll(yi)  // 4xQMAC
            ;

        convolved.compute_at(output_, yo)
            .vectorize(n, np_size)
            .unroll(y);

        convolved.update(0)
            .split(k.z, co, ci, cp_size)
            .split(ci, cio, cii, vector_reduction)  // QMAC
            .reorder(n, cii, y, cio, co, k.y, k.x, x)
            .vectorize(n, np_size)
            .unroll(y)    // 4xQMAC
            .unroll(cio)  // cp x QMAC
            .atomic()
            .vectorize(cii, vector_reduction)  // QMAC
            ;

        input_.set_host_alignment(64);
        filter_.set_host_alignment(64);
        output_.set_host_alignment(64);

        input_.dim(0)
            .set_min(0)
            .set_extent((input_.dim(0).extent() / 64) * 64);
        input_.dim(1)
            .set_min(0);
        input_.dim(2)
            .set_min(0);

        filter_.dim(0)
            .set_min(0)
            .set_extent((filter_.dim(0).extent() / 64) * 64);
        filter_.dim(1)
            .set_min(0);
        filter_.dim(2)
            .set_min(0);
        filter_.dim(3)
            .set_min(0);

        output_.dim(0)
            .set_min(0)
            .set_extent((output_.dim(0).extent() / 64) * 64);
        output_.dim(1)
            .set_min(0);
        input_.dim(2)
            .set_min(0);

        RequireAlignedRows(input_, 64);
        RequireAlignedRows(filter_, 64);
        RequireAlignedRows(output_, 64);
    }
};

}  // namespace interpret_nn

HALIDE_REGISTER_GENERATOR(interpret_nn::Convolution, Convolution)
