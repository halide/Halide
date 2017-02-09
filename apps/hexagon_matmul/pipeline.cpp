#include "Halide.h"

using namespace Halide;
using namespace Halide::ConciseCasts;

int main(int argc, char **argv) {
    Target target = get_target_from_environment();

    std::cout << "Target: " << target.to_string() << "\n";

    // We take two 8 bit matrices as input.
    // The matrices are indexed by (row, col), so it is the second
    // dimension that has unit stride (by default, the first dimension
    // has a unit stride).
    Var i("i"), j("j");
    ImageParam A(UInt(8), 2);
    ImageParam B(UInt(8), 2);

    // Align the extent of the K dimension to the product of our split
    // factors.
    const int k_split_factor = 8;
    Expr k_extent = A.dim(1).extent();
    k_extent = (k_extent/(k_split_factor*4))*(k_split_factor*4);
    A.dim(1).set_extent(k_extent);
    B.dim(0).set_extent(k_extent);

    // We split directly in the algorithm by a factor of 4, so we can
    // generate vrmpy instructions on Hexagon.
    RDom rk(0, k_extent/4, "k");

    Func AB("AB");
    AB(i, j) = u32(0);
    AB(i, j) +=
        u32(u16(A(i, 4*rk + 0))*u16(B(4*rk + 0, j))) +
        u32(u16(A(i, 4*rk + 1))*u16(B(4*rk + 1, j))) +
        u32(u16(A(i, 4*rk + 2))*u16(B(4*rk + 2, j))) +
        u32(u16(A(i, 4*rk + 3))*u16(B(4*rk + 3, j)));

    // Schedule.
    int vector_size_u8 = target.natural_vector_size<uint8_t>();
    if (target.features_any_of({Target::HVX_64, Target::HVX_128})) {
        vector_size_u8 = target.has_feature(Target::HVX_128) ? 128 : 64;
    }

    AB.compute_root()
        .vectorize(j, vector_size_u8, TailStrategy::RoundUp)
        .parallel(i, 16);

    if (target.features_any_of({Target::HVX_64, Target::HVX_128})) {
        // Some comments on the schedule:
        // - Vector loads from B are expensive, because vrmpy requires
        //   interleaving four of them. So, we try to order the loops
        //   such that those loads are lifted to an outer loop.
        // - Loads from A are small, so as many columns as possible
        //   should be loaded together to maximize the utilization of
        //   cache lines that are loaded.
        // - There's probably a good Z-order traversal that could be
        //   used here to improve locality (TODO).
        RVar rki, rko, rkoo, rkoi;
        AB.update(0)
            .split(rk, rko, rki, k_split_factor)
            .split(rko, rkoo, rkoi, 8)
            .reorder(rki, rkoi, i, rkoo, j)
            .vectorize(j, vector_size_u8, TailStrategy::RoundUp)
            .prefetch(i)
            .unroll(rki)
            .parallel(j);

        AB.hexagon();
        AB.update(0).hexagon();
    } else {
        Var ji("ji"), jii("jii"), ii("ii"), iii("iii");
        RVar rki("rki");

        // This schedule taken from test/performance/matrix_multiplication.cpp
        constexpr int kBlockSize = 32;
        const int kBlockSizeJi = 8;
        AB.update(0)
            .split(j, j, ji, kBlockSize, TailStrategy::GuardWithIf)
            .split(ji, ji, jii, kBlockSizeJi, TailStrategy::GuardWithIf)
            .split(i, i, ii, kBlockSize, TailStrategy::GuardWithIf)
            .split(ii, ii, iii, 4, TailStrategy::GuardWithIf)
            .split(rk, rk, rki, kBlockSize, TailStrategy::GuardWithIf)
            .reorder(jii, iii, ji, rki, ii, rk, j, i)
            .parallel(i)
            .vectorize(jii)
            .unroll(ji)
            .unroll(iii);
    }

    // Require scanlines of the input and output to be aligned, and
    // tell Halide about our dimension convention.
    for (auto i : {(OutputImageParam)A, (OutputImageParam)B, AB.output_buffer()}) {
        int align = vector_size_u8/i.type().bytes();
        i.dim(0)
            .set_bounds(0, (i.dim(0).extent()/align)*align)
            .set_stride((i.dim(0).stride()/align)*align);

        i.dim(1)
            .set_bounds(0, (i.dim(1).extent()/align)*align)
            .set_stride(1);
    }

    std::stringstream hdr;
    hdr << argv[2] << ".h";
    AB.compile_to_header(hdr.str(), {A, B}, argv[2], target);
    std::stringstream obj;
    obj << argv[1] << ".o";
    AB.compile_to_object(obj.str(), {A, B}, argv[2], target);

    return 0;
}
