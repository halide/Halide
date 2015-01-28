#include "Halide.h"

using namespace Halide;

namespace {

class MatrixMultiply : public Generator<MatrixMultiply> {
  public:
    GeneratorParam<int> algorithm{ "algorithm", 0 };
    ImageParam A_in{ Float(32), 2, "A_in" };
    ImageParam B_in{ Float(32), 2, "B_in" };

    Func build() override {
        // Expr size = (A_in.width() / 32) * 32;
        if (algorithm == 0) {
            Matrix A(A_in, "A");
            Matrix B(B_in, "B");

            Matrix C = A * B;

            return static_cast<Func>( C ).compute_root();
        } else if (algorithm == 1) {
            const int block_size = 32;
            const int vec_size = natural_vector_size<float>();

            const Expr num_rows = A_in.width();
            const Expr num_cols = B_in.height();
            const Expr sum_size = A_in.height();
            const Expr proxy_size = ((sum_size + block_size - 1) / block_size) * block_size;

            Var x("x"), xi("xi"), xo("xo"), y("y"), yo("yo"), yi("yi"), yii("yii"), xii("xii"), task("task");
            Func A("A"), B("B"), C("C"), prod("prod");

            RDom k(0, proxy_size);
            RVar ki;

            A(x, y) = //A_in(x, y);
                select(0 <= x && x < num_rows,
                       select(0 <= y && y < sum_size, A_in(x, y),
                              select(x == y, 1.0f, 0.0f)),
                       select(x == y, 1.0f, 0.0f));
            B(x, y) = //B_in(x, y);
                select(0 <= x && x < sum_size,
                       select(0 <= y && y < num_cols, B_in(x, y),
                              select(x == y, 1.0f, 0.0f)),
                       select(x == y, 1.0f, 0.0f));
            prod(x, y) += select(k < sum_size, A(x, k) * B(k, y), 0.0f);
            C(x, y) = prod(x, y);

            // A.bound(x, 0, num_rows).bound(y, 0, proxy_size);
            // B.bound(x, 0, proxy_size).bound(y, 0, num_cols);
            C.tile(x, y, xi, yi, block_size, block_size).parallel(y)
                    .vectorize(xi, vec_size).unroll(xi);//.unroll(yi);

            prod.compute_at(C, x);
            prod.vectorize(x, vec_size).unroll(x);//.unroll(y);
            prod.update(0)
                    .split(k, k, ki, block_size)
                    .reorder(x, y, ki, k)
                    .vectorize(x, vec_size).unroll(x);//.unroll(y);
                    // .parallel(y);

            A.compute_at(prod, k).vectorize(x, vec_size).unroll(x);//.unroll(y);
            B.compute_at(prod, k).vectorize(x, vec_size).unroll(x);//.unroll(y);

            C.bound(x, 0, num_rows).bound(y, 0, num_cols);

            return C;
        } else if (algorithm == 2) {
            const int block_size = 32;

            const Expr num_rows = A_in.width();
            const Expr num_cols = B_in.height();
            const Expr size = (num_rows / block_size) * block_size;

            Var x("x"), xi("xi"), xo("xo"), y("y"), yo("yo"), yi("yi"), yii("yii"), xii("xii");
            Func A("A"), B("B"), C("C"), prod("prod");
            A(xi, yi, x, y) = A_in(x * block_size + xi, y * block_size + yi);
            B(xi, yi, x, y) = B_in(x * block_size + xi, y * block_size + yi);

            RDom k(0, size);
            RVar ki;
            prod(xi, yi, x, y) += A(xi, k % block_size, x, k / block_size)
                    * B(k % block_size, yi, k / block_size, y);
            C(x, y) = prod(x % block_size, y % block_size, x / block_size, y / block_size);

            // prod.vectorize(xi, 8).unroll(xi).unroll(yi);
            prod.update(0).tile(xi, yi, xii, yii, 8, 4)
                    .split(k, k, ki, block_size)
                    .reorder(xii, yii, ki, xi, yi, k, x, y)
                    .vectorize(xii).unroll(yii).unroll(xi).unroll(yi);
                    // .vectorize(k, 8).unroll(k).unroll(xi);//.unroll(yii);

            A.compute_at(prod, x).vectorize(xi, 8).unroll(xi).unroll(yi);
            B.compute_at(prod, x).vectorize(xi, 8).unroll(xi).unroll(yi);
            C.tile(x, y, xi, yi, block_size, block_size).vectorize(xi, 8).unroll(xi).unroll(yi).parallel(y);
            C.bound(x, 0, size).bound(y, 0, size);
            prod.compute_at(C, x);

            C.output_buffer().set_bounds(0, 0, 64).set_bounds(1, 0, 64);

            return C;
        } else {
            const Expr num_rows = A_in.width();
            const Expr num_cols = B_in.height();
            const Expr size = num_rows;

            Func dot, C;

            Var ti("ti"), tj("tj"), tti("tti"), ttj("ttj");
            Var i("i"), j("j");

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
            dot(k, i, j) += A_in(sum_vecs*vec_size + k, i) * Bt(sum_vecs*vec_size + k, j);

            RDom sum_lanes(0, vec_size);
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
