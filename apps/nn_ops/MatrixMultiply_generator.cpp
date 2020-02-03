// This generator implements a quantized matrix multiplication and schedules for
// HVX and CPU. The generator schedule assumes certain size constraints on the
// two input matrices:
// * The width of the left-hand-side (mat_a_ below) must be divisible by 4.
// * The height of mat_a_ must be divisible by 4.
// * The width of the right-hand-side (mat_b_ below) must be divisible by
//   the natural vector size of the architecture you want to run this code on.
// Note that all these constraints are asserted at runtime, so running with
// illegal sizes with trigger those assertions. Correct input sizes can be
// achieved by padding mat_a_ with the value -mat_a_offset_ and mat_b_ with the
// value -mat_b_offset_.

#include "common.h"
#include <Halide.h>

using Halide::Generator;
using Halide::RVar;
using Halide::ConciseCasts::i32;
using Halide::ConciseCasts::u16;
using Halide::ConciseCasts::u32;
using Halide::ConciseCasts::u8_sat;

class MatrixMultiply : public Generator<MatrixMultiply> {
public:
    // Two unsigned 8-bit input matrices, indexed by x, y.
    Input<Buffer<uint8_t>> mat_a_{"mat_a", 2};
    Input<Buffer<uint8_t>> mat_b_{"mat_b", 2};

    // A 1D array of 32-bit biases indexed by output width.
    Input<Buffer<int32_t>> bias_{"bias", 1};

    // Offsets and multipliers for the input, filter, and output.
    Input<int16_t> mat_a_offset_{"mat_a_offset", 0, -255, 0};
    Input<int16_t> mat_b_offset_{"mat_b_offset", 0, -255, 0};
    Input<int> output_multiplier_{"output_multiplier"};
    Input<int> output_shift_{"output_shift"};
    Input<int> output_offset_{"output_offset", 0, 0, 255};
    Input<uint8_t> output_min_{"output_min"};
    Input<uint8_t> output_max_{"output_max"};

    Output<Buffer<uint8_t>> output_{"output", 2};

    void generate() {
        // We take two 8 bit matrices as input.
        Var x("x"), y("y");

        // Align the extent of the k dimension to the unroll factor used in the
        // reduction. Unrolling there is needed to use the vrmpy instruction on
        // Hexagon.
        constexpr int kDotProductUnrollFactor = 4;
        Expr k_extent = mat_a_.dim(0).extent();
        k_extent =
            (k_extent / (kDotProductUnrollFactor)) * (kDotProductUnrollFactor);
        mat_a_.dim(0).set_extent(k_extent);
        mat_b_.dim(1).set_extent(k_extent);

        // We split directly in the algorithm by a factor of 4
        // (== kDotProductUnrollFactor), so we can generate vrmpy instructions on
        // Hexagon.
        RDom rk(0, k_extent / kDotProductUnrollFactor, "k");

        int vector_size_u8 = natural_vector_size<uint8_t>();
        int vector_size_u32 = natural_vector_size<uint32_t>();
        bool use_hexagon = false;
        if (get_target().has_feature(Halide::Target::HVX_64)) {
            vector_size_u8 = 64;
            vector_size_u32 = 16;
            use_hexagon = true;
        } else if (get_target().has_feature(Halide::Target::HVX_128)) {
            vector_size_u8 = 128;
            vector_size_u32 = 32;
            use_hexagon = true;
        }

        // Define the reordering of mat_b_ as a separate stage so we can lift
        // the interleaving required by vrmpy out of the inner loop.
        Func mat_b_swizzled("mat_b_swizzled");
        Var k("k");
        mat_b_swizzled(x, y, k) = mat_b_(x, 4 * y + k);

        // We need to compute the matrix product:
        //   (mat_a_ + mat_a_offset_ * 1_a) * (mat_b_ + mat_b_offset_ * 1_b),
        // where
        //   * mat_a_ and mat_b_ are 8-bit input matrices,
        //   * mat_a_offset_ and mat_b_offset_ are scalar values in [-255, 0],
        //   * 1_a is the matrix of same size as mat_a_ filled with 1s,
        //   * 1_b is the matrix of same size as mat_b_ filled with 1s.
        // If we add the offsets upfront then the matrix multiplication has to be
        // carried out on 16-bit input matrices. To take full advantage of the
        // available instructions we need to hit the correct pattern, which is
        // the expression defining the Func multiplied_no_offsets below.
        //
        // Hence we have to factor the computation into the following four products
        // (1) mat_a_ * mat_b_
        // (2) mat_a_offset_ * 1_a * mat_b_
        // (3) mat_b_offset_ * mat_a_ * 1_b
        // (4) mat_a_offset_ * mat_b_offset_ * 1_a * 1_b
        // Product (1) is the main matrix multiplication that we carry out with
        // multiplied_no_offsets.
        // Product (2) can be computed by computing the column sums of mat_b_ (dot
        // product with the vector containing only 1s) and replicating the result
        // into the right shape and multiplying by mat_a_offset_.
        // Product (3) can be computed by computing the row sums of mat_a_ (dot
        // product with the vector containing only 1s), replicating the result
        // into the right shape and multiplying with mat_b_offset_.
        // Finally, the product (4) is just
        // mat_a_offset_ * mat_b_offset_ * mat_a_.width() replicated to every
        // element of the resulting matrix.
        Func multiplied_no_offsets("multiplied_no_offsets");
        multiplied_no_offsets(x, y) = u32(0);
        multiplied_no_offsets(x, y) +=
            u32(u16(mat_a_(4 * rk + 0, y)) * u16(mat_b_swizzled(x, rk, 0))) +
            u32(u16(mat_a_(4 * rk + 1, y)) * u16(mat_b_swizzled(x, rk, 1))) +
            u32(u16(mat_a_(4 * rk + 2, y)) * u16(mat_b_swizzled(x, rk, 2))) +
            u32(u16(mat_a_(4 * rk + 3, y)) * u16(mat_b_swizzled(x, rk, 3)));

        RDom fk(0, mat_a_.width(), "fk");

        // We could convert the row sums into a partial horizontal reduction that is
        // vectorized. Partial results can be summed up by a scalar sum afterwards.
        // While there is a performance benefit for large matrices, we did not
        // observe any performance difference on practical models. So for simplicity
        // we just use the straightforward row sum computation here.
        Func row_sums_a("row_sums_a");
        row_sums_a(y) = sum(u32(mat_a_(fk, y)));

        Func column_sums_b("column_sums_b");
        column_sums_b(x) = sum(u32(mat_b_(x, fk)));

        Expr offset =
            cast<int32_t>(mat_a_offset_) * cast<int32_t>(mat_b_offset_) * mat_a_.width();

        Func multiplied("multiplied");
        multiplied(x, y) =
            multiplied_no_offsets(x, y) +
            i32(mat_a_offset_) * i32(column_sums_b(x)) +
            i32(mat_b_offset_) * i32(row_sums_a(y)) + offset;

        // Scale the output.
        Func scaled_plus_offset("scaled_plus_offset");
        scaled_plus_offset(x, y) =
            multiply_quantized_multiplier(multiplied(x, y) + bias_(x),
                                          output_multiplier_, output_shift_) +
            output_offset_;

        // Saturate and narrow the output.
        output_(x, y) =
            clamp(u8_sat(scaled_plus_offset(x, y)), output_min_, output_max_);

        // Specifying .hexagon() on a Func will generate an RPC to run this stage
        // on Hexagon. If Hexagon is the host (that is, the architecture is
        // Hexagon), we have to omit the .hexagon() directive as we are already
        // running on Hexagon.
        if (use_hexagon && get_target().arch != Target::Hexagon) {
            output_.hexagon();
        }

        constexpr int kTileSizeHeight = 4;
        if (use_hexagon) {
            Var xo("xo"), yo("yo");

            // Split the output into tiles, traversed in columns of tiles
            // that we parallelize over.
            output_.compute_root()
                .tile(x, y, xo, yo, x, y, vector_size_u8, kTileSizeHeight,
                      TailStrategy::RoundUp)
                .reorder(yo, xo)
                .prefetch(mat_a_, yo)
                .vectorize(x)
                .unroll(y)
                .parallel(xo);

            // Compute the product at tiles of the output.
            multiplied_no_offsets.compute_at(output_, yo).vectorize(x).unroll(y);

            multiplied_no_offsets.update(0).reorder(x, y, rk).vectorize(x).unroll(y);

            // Lift the swizzling out of the inner loop.
            mat_b_swizzled.compute_at(output_, xo)
                .reorder_storage(k, x, y)
                .reorder(k, x, y)
                .vectorize(x)
                .unroll(k);

            // Split the rows into chunks we can parallelize over, but prefetch
            // within.
            Var yi("yi");
            row_sums_a.compute_at(output_, Var::outermost())
                .split(y, y, yi, 32)
                .parallel(y)
                .prefetch(mat_a_, yi);

            Var xi("xi");
            column_sums_b.compute_at(output_, Var::outermost())
                .split(x, x, xi, vector_size_u8, TailStrategy::GuardWithIf)
                .parallel(x)
                .vectorize(xi);

        } else {
            Var xi("xi"), xii("xii"), yi("yi"), yii("yii");
            RVar rki("rki");

            // This schedule taken from test/performance/MatrixMultiply.cpp
            constexpr int kBlockSize = 32;
            constexpr int kBlockSizeXi = 8;

            output_.compute_root()
                .tile(x, y, x, y, xi, yi, vector_size_u8, kTileSizeHeight,
                      TailStrategy::RoundUp)
                .reorder(xi, yi, x, y)
                .vectorize(xi)
                .unroll(yi)
                .parallel(y);

            multiplied_no_offsets.compute_root().vectorize(x, vector_size_u32);

            multiplied_no_offsets.update(0)
                .split(x, x, xi, kBlockSize, TailStrategy::GuardWithIf)
                .split(xi, xi, xii, kBlockSizeXi, TailStrategy::GuardWithIf)
                .split(y, y, yi, kBlockSize, TailStrategy::GuardWithIf)
                .split(yi, yi, yii, 4, TailStrategy::GuardWithIf)
                .split(rk, rk, rki, kBlockSize, TailStrategy::GuardWithIf)
                .reorder(xii, yii, xi, rki, yi, rk, x, y)
                .parallel(y)
                .vectorize(xii)
                .unroll(xi)
                .unroll(yii);

            row_sums_a.compute_root().vectorize(y, vector_size_u8,
                                                TailStrategy::ShiftInwards);

            column_sums_b.compute_root().vectorize(x, vector_size_u8,
                                                   TailStrategy::ShiftInwards);
        }

        constexpr int kMatAHeightAlign = kTileSizeHeight;
        int vector_dim_align = vector_size_u8;

        mat_a_.dim(0)
            .set_bounds(0, mat_a_.dim(0).extent())
            .dim(1)
            .set_bounds(0, (mat_a_.dim(1).extent() / kMatAHeightAlign) * kMatAHeightAlign)
            .set_stride((mat_a_.dim(1).stride() / kMatAHeightAlign) *
                        kMatAHeightAlign);

        mat_b_.dim(0)
            .set_bounds(0, (mat_b_.dim(0).extent() / vector_dim_align) * vector_dim_align)
            .dim(1)
            .set_bounds(0, mat_b_.dim(1).extent())
            .set_stride(mat_b_.dim(1).stride());

        output_.dim(0)
            .set_bounds(0, (output_.dim(0).extent() / vector_dim_align) * vector_dim_align)
            .dim(1)
            .set_bounds(0, (output_.dim(1).extent() / kMatAHeightAlign) * kMatAHeightAlign)
            .set_stride((output_.dim(1).stride() / kMatAHeightAlign) * kMatAHeightAlign);

        bias_.dim(0).set_bounds(0, bias_.dim(0).extent());
    }
};

HALIDE_REGISTER_GENERATOR(MatrixMultiply, MatrixMultiply)
