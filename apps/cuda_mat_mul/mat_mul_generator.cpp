#include "Halide.h"

using namespace Halide;

namespace {

void set_alignment_and_bounds(OutputImageParam p, int size) {
    p.set_host_alignment(16)
        .dim(0)
        .set_bounds(0, size)
        .dim(1)
        .set_bounds(0, size)
        .set_stride(size);
}

class MatMul : public Halide::Generator<MatMul> {
public:
    GeneratorParam<int> size{"size", 1024};
    // tile_ir=true: f16 inputs, f32 acc/output, tile-IR-friendly schedule
    // (the matmul-pattern matcher in CodeGen_TileIR_Dev → mmaf → HMMA).
    GeneratorParam<bool> tile_ir{"tile_ir", false};

    Input<Buffer<void, 2>> A{"A"};
    Input<Buffer<void, 2>> B{"B"};

    Output<Buffer<float, 2>> out{"out"};

    void configure() {
        Type in_t = tile_ir ? Float(16) : Float(32);
        A.set_type(in_t);
        B.set_type(in_t);
    }

    void generate() {
        Var x("x"), y("y");

        // Algorithm — same regardless of backend. The cast lets us drive
        // the same expression with either f32 or f16 inputs.
        Func prod("prod");
        RDom r(0, size);
        prod(x, y) += cast<float>(A(x, r)) * cast<float>(B(r, y));
        out(x, y) = prod(x, y);

        if (using_autoscheduler()) {
            A.dim(0).set_estimate(0, size).dim(1).set_estimate(0, size);
            B.dim(0).set_estimate(0, size).dim(1).set_estimate(0, size);
        } else if (tile_ir) {
            // Tile-IR schedule
            Var xi{"xi"}, yi{"yi"}, xii{"xii"}, yii{"yii"};
            RVar ko{"ko"}, ki{"ki"};
            out.tile(x, y, xi, yi, 128, 128)
                .gpu_blocks(x, y)
                .gpu_threads(xi, yi);

            prod.compute_at(out, x)
                .gpu_threads(x, y)
                .update()
                .atomic()
                .split(r, ko, ki, 16)
                .gpu_threads(x, y)
                .reorder(ki, x, y, ko)
                .vectorize(ki);

            set_alignment_and_bounds(A, size);
            set_alignment_and_bounds(B, size);
            set_alignment_and_bounds(out, size);
        } else {
            // Original f32 CUDA schedule.
            Var xi, yi, xio, xii, yii;
            out.tile(x, y, xi, yi, 64, 16)
                .tile(xi, yi, xio, yii, 4, 8)
                .gpu_blocks(x, y)
                .gpu_threads(xi, yi)
                .unroll(xio)
                .unroll(yii);
            prod.compute_at(out, xi)
                .vectorize(x)
                .unroll(y)
                .update()
                .reorder(x, y, r)
                .vectorize(x)
                .unroll(y)
                .unroll(r, 8);
            A.in().compute_at(prod, r).vectorize(_0).unroll(_1);
            B.in().compute_at(prod, r).vectorize(_0).unroll(_1);

            set_alignment_and_bounds(A, size);
            set_alignment_and_bounds(B, size);
            set_alignment_and_bounds(out, size);
        }

        out.bound(x, 0, size).bound(y, 0, size);
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(MatMul, mat_mul)
