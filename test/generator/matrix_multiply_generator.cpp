#include "Halide.h"

using namespace Halide;

namespace {

class MatrixMultiply : public Generator<MatrixMultiply> {
public:
    GeneratorParam<bool> use_matrix_class{ "use_matrix_class", false };
    ImageParam A_in{ Int(32), 2, "A_in" };
    ImageParam B_in{ Int(32), 2, "B_in" };

    Func build() override {
        Expr size = A_in.width();

        if (use_matrix_class) {
            Matrix A(A_in, "A");
            Matrix B(B_in, "B");

            Matrix C = A * B;

            return static_cast<Func>( C ).compute_root();
        } else {
            Var i("i"), j("j");
            Var ti("ti"), tj("tj"), tti("tti"), ttj("ttj");

            // Pretranspose B so we can take dot products of rows.
            Func Bt;
            Bt(i, j) = B_in(j, i);

            // Compute a dot product of a row in A and a row in Bt. First
            // accumulate in vectors, and then accumulate the lanes in
            // scalar code at the end. This assumes that S is a multiply
            // of vec_size.
            const int vec_size = 8;

            RDom sum_vecs(0, size/vec_size);
            Var k("k");
            Func dot("dot");
            dot(k, i, j) += A_in(sum_vecs*vec_size + k, i) * Bt(sum_vecs*vec_size + k, j);

            RDom sum_lanes(0, vec_size);
            Func C("C");
            C(i, j) = sum(dot(sum_lanes, i, j));

            // Compute the result in 16 x 16 tiles, with each row of tiles
            // on a separate core. Split each tile recursively into four
            // 8x8 sub-tiles to compute the dot products.
            C.tile(i, j, ti, tj, i, j, 16, 16).tile(i, j, tti, ttj, i, j, 8, 8).parallel(tj);

            // Compute the dot product per sub-tile. Vectorize it, and
            // unroll across the sub-tile.
            dot.compute_at(C, tti).vectorize(k);
            dot.update()
                    .reorder(k, i, j, sum_vecs).vectorize(k)
                    .unroll(i).unroll(j);

            // Compute B transpose per-core as needed in 16x16 tiles.
            Bt.compute_at(C, tj).tile(i, j, ti, tj, i, j, 16, 16);

            return C;
        }
    }
};

RegisterGenerator<MatrixMultiply> register_my_gen("matrix_multiply");

}
