#include "Halide.h"

using namespace Halide;
using namespace Halide::ConciseCasts;

int main(int argc, char **argv) {
    Target target = get_target_from_environment();

    std::cout << "Target: " << target.to_string() << "\n";

    // We take two 8 bit matrices as input.
    Var x("x"), y("y");
    ImageParam A(UInt(8), 2);
    ImageParam B(UInt(8), 2);

    // Align the extent of the K dimension to the product of our split
    // factors.
    const int k_split_factor = 32;
    Expr k_extent = A.dim(0).extent();
    k_extent = (k_extent/(k_split_factor*4))*(k_split_factor*4);
    A.dim(0).set_extent(k_extent);
    B.dim(1).set_extent(k_extent);

    // We split directly in the algorithm by a factor of 4, so we can
    // generate vrmpy instructions on Hexagon.
    RDom rk(0, k_extent/4, "k");

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
    Func output;
    output(x, y) = AB(x, y);

    // Schedule.
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
        output.compute_root()
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
            .unroll(y);

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

        output.compute_root()
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
            .split(rk, rk, rki, kBlockSize, TailStrategy::GuardWithIf)
            .reorder(xii, yii, xi, rki, yi, rk, x, y)
            .parallel(y)
            .vectorize(xii)
            .unroll(xi)
            .unroll(yii);
    }

    // Require scanlines of the input and output to be aligned, and
    // tell Halide about our dimension convention.
    for (auto i : {(OutputImageParam)A, (OutputImageParam)B, output.output_buffer()}) {
        int align = vector_size_u8/i.type().bytes();
        i.dim(0)
            .set_bounds(0, (i.dim(0).extent()/align)*align);

        i.dim(1)
            .set_bounds(0, (i.dim(1).extent()/align)*align)
            .set_stride((i.dim(1).stride()/align)*align);
    }

    std::stringstream hdr;
    hdr << argv[2] << ".h";
    output.compile_to_header(hdr.str(), {A, B}, argv[2], target);
    std::stringstream obj;
    obj << argv[1] << ".o";
    output.compile_to_object(obj.str(), {A, B}, argv[2], target);

    return 0;
}
