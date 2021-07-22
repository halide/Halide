#include "Halide.h"

using namespace Halide;
using namespace Halide::ConciseCasts;

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
    GeneratorParam<int> size{"size", 2048};
    Input<Buffer<float16_t>> A{"A", 2};
    Input<Buffer<float16_t>> B{"B", 2};

    Output<Buffer<float>> out{"out", 2};

    void generate() {
        // 688 us on an RTX 2060
        // cublas is 512 us on the same card

        Var x("x"), y("y"), p("p");

        Func prod("prod");
        RDom r(0, size);
        prod(x, y) += f32(A(x, r)) * f32(B(r, y));

        Var xi, yi, xio, xii, yii, xo, yo, x_pair, xiio, ty;
        RVar rxo, rxi;

        if (get_target().features_any_of({Target::CUDACapability70, Target::CUDACapability75, Target::CUDACapability80})) {
            out = prod;
            out
                .gpu_tile(x, y, xo, yo, xi, yi, 16, 16)
                .update()
                .gpu_tile(x, y, xo, yo, xi, yi, 16, 16);

        } else {
            out(x, y) = prod(x, y);
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
        }

        set_alignment_and_bounds(A, size);
        set_alignment_and_bounds(B, size);
        set_alignment_and_bounds(out, size);
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(MatMul, mat_mul_50)
HALIDE_REGISTER_GENERATOR(MatMul, mat_mul_70)
