#include "Halide.h"

using namespace Halide;

namespace {

void set_alignment_and_bounds(OutputImageParam p, int size) {
    p.set_host_alignment(16)
        .dim(0)
        .set_bounds(0, size)
        .dim(1)
        .set_stride(size);
}

class MatMul : public Halide::Generator<MatMul> {
public:
    GeneratorParam<int> size{"size", 1024};
    Input<Buffer<float, 2>> A{"A"};
    Input<Buffer<float, 2>> B{"B"};

    Output<Buffer<float, 2>> out{"out"};

    void generate() {
        // 688 us on an RTX 2060
        // cublas is 512 us on the same card

        Var x("x"), y("y"), p("p");

        Func prod("prod");
        RDom r(0, size);
        prod(x, y) += A(x, r) * B(r, y);
        out(x, y) = prod(x, y);

        Var xi, yi, xio, xii, yii, xo, yo, x_pair, xiio, ty;
        RVar rxo, rxi;

        out.bound(x, 0, size)
            .bound(y, 0, size)
            .tile(x, y, xi, yi, 64, 16)
            .tile(xi, yi, xii, yii, 4, 8)
            .gpu_blocks(x, y)
            .gpu_threads(xi, yi)
            .unroll(xii)
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
};

}  // namespace

HALIDE_REGISTER_GENERATOR(MatMul, mat_mul)
