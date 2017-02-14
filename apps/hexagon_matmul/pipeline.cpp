#include "Halide.h"

using namespace Halide;
using namespace Halide::ConciseCasts;

class MatMul : public Generator<MatMul> {
public:
    // We take 2 8-bit matrices as input.
    Input<Buffer<uint8_t>> A{"A", 2};
    Input<Buffer<uint8_t>> B{"B", 2};

    // We produce a 32 bit matrix result.
    Output<Buffer<uint32_t>> output{"output", 2};

    std::function<void()> schedule;

    void generate() {
        Var x{"x"}, y{"y"};

        // Align the extent of the K dimension to the product of our split
        // factors.
        const int k_unroll_factor = 2;
        Expr k_extent = A.dim(0).extent();
        k_extent = (k_extent/(k_unroll_factor*4))*(k_unroll_factor*4);

        // We split directly in the algorithm by a factor of 4, so we can
        // generate vrmpy instructions on Hexagon.
        const int k_split_factor = 4;
        RDom rk(0, k_extent/k_split_factor, "k");

        // Define the reordering of B as a separate stage so we can lift
        // the interleaving required by vrmpy out of the inner loop.
        Func B_swizzled("B_swizzled");
        Var k("k");
        B_swizzled(x, y, k) = B(x, 4*y + k);

        Func AB("AB");
        AB(x, y) = u32(0);
        AB(x, y) +=
            u32(u16(A(4*rk + 0, y))*u16(B_swizzled(x, rk, 0))) +
            u32(u16(A(4*rk + 1, y))*u16(B_swizzled(x, rk, 1))) +
            u32(u16(A(4*rk + 2, y))*u16(B_swizzled(x, rk, 2))) +
            u32(u16(A(4*rk + 3, y))*u16(B_swizzled(x, rk, 3)));

        // We need a wrapper for the output so we can schedule the
        // multiply update in tiles.
        output(x, y) = AB(x, y);

        // Schedule.
        schedule = [=]() mutable {
            const Target &target = get_target();

            int vector_size_u8 = target.natural_vector_size<uint8_t>();
            bool use_hexagon = false;
            if (target.has_feature(Target::HVX_64)) {
                vector_size_u8 = 64;
                use_hexagon = true;
            } else if (target.has_feature(Target::HVX_128)) {
                vector_size_u8 = 128;
                use_hexagon = true;
            }
            int vector_size_u32 = vector_size_u8 / 4;

            if (use_hexagon) {
                Var xo("xo"), yo("yo");

                // Split the output into tiles, traversed in columns of tiles
                // that we parallelize over.
                Func(output).compute_root()
                    .hexagon()
                    .tile(x, y, xo, yo, x, y, vector_size_u8, 4, TailStrategy::RoundUp)
                    .reorder(yo, xo)
                    .vectorize(x)
                    .unroll(y)
                    .parallel(xo);

                // Compute the product at tiles of the output.
                AB.compute_at(output, yo)
                    .vectorize(x)
                    .unroll(y);

                AB.update(0)
                    .reorder(x, y, rk)
                    .vectorize(x)
                    .unroll(y)
                    .unroll(rk, k_unroll_factor);

                // Lift the swizzling out of the inner loop.
                B_swizzled.compute_at(output, xo)
                    .reorder_storage(k, x, y)
                    .reorder(k, x, y)
                    .vectorize(x, vector_size_u8, TailStrategy::RoundUp)
                    .unroll(k);
            } else {
                Var xi("xi"), xii("xii"), yi("yi"), yii("yii");
                RVar rki("rki");

                // This schedule taken from test/performance/matrix_multiplication.cpp
                constexpr int kBlockSize = 32;
                const int kBlockSizeXi = 8;

                Func(output).compute_root()
                    .tile(x, y, x, y, xi, yi, vector_size_u8, 4, TailStrategy::RoundUp)
                    .reorder(xi, yi, x, y)
                    .vectorize(xi)
                    .unroll(yi)
                    .parallel(y);

                AB.compute_root()
                    .vectorize(x, vector_size_u32);

                AB.update(0)
                    .split(x, x, xi, kBlockSize, TailStrategy::GuardWithIf)
                    .split(xi, xi, xii, kBlockSizeXi, TailStrategy::GuardWithIf)
                    .split(y, y, yi, kBlockSize, TailStrategy::GuardWithIf)
                    .split(yi, yi, yii, 4, TailStrategy::GuardWithIf)
                    .split(rk, rk, rki, kBlockSize/k_split_factor, TailStrategy::GuardWithIf)
                    .reorder(xii, yii, xi, rki, yi, rk, x, y)
                    .parallel(y)
                    .vectorize(xii)
                    .unroll(xi)
                    .unroll(yii);
            }

            // Require scanlines of the input and output to be aligned.
            A.dim(0)
                .set_bounds(0, (k_extent/vector_size_u8)*vector_size_u8);
            A.dim(1)
                .set_bounds(0, (A.dim(1).extent()/vector_size_u8)*vector_size_u8)
                .set_stride((A.dim(1).stride()/vector_size_u8)*vector_size_u8);
            B.dim(0)
                .set_bounds(0, (B.dim(0).extent()/vector_size_u8)*vector_size_u8);
            B.dim(1)
                .set_bounds(0, (k_extent/vector_size_u8)*vector_size_u8)
                .set_stride((B.dim(1).stride()/vector_size_u8)*vector_size_u8);
            output.dim(0)
                .set_bounds(0, (output.dim(0).extent()/vector_size_u32)*vector_size_u32);
            output.dim(1)
                .set_bounds(0, (output.dim(1).extent()/vector_size_u32)*vector_size_u32)
                .set_stride((output.dim(1).stride()/vector_size_u32)*vector_size_u32);

        };
    }
};

HALIDE_REGISTER_GENERATOR(MatMul, "matmul");
