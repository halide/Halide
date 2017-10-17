#include "Halide.h"

using namespace Halide;

namespace {

class MatMul : public Halide::Generator<MatMul> {
public:

    GeneratorParam<int>   size {"size", 1024};
    Input<Buffer<float>>  A{"A", 2};
    Input<Buffer<float>>  B{"B", 2};

    Output<Buffer<float>> out{"out", 2};

    void generate() {
        Var x("x"), y("y");

        Func prod("prod");
        RDom r(0, size);
        prod(x, y) += A(x, r) * B(r, y);

        Var xi, yi, xio, xii, yii, xo;
        out(x, y) = prod.in()(x, y);
        out.bound(x, 0, size)
            .bound(y, 0, size)
            .tile(x, y, xi, yi, 8*32, 8)
            .split(xi, xio, xii, 32)
            .reorder(xio, yi, xii, x, y)
            .unroll(xio)
            .unroll(yi)
            .gpu_blocks(x, y).gpu_threads(xii);
        prod.compute_at(out, xii)
            .unroll(x)
            .unroll(y)
            .update()
            .unroll(r.x, 2)
            .reorder(y, x, r.x)
            .unroll(x)
            .unroll(y);
        B.in()
            .compute_at(prod, y)
            .vectorize(B.in().args()[0]);

        const DimensionedParam bufs[] = {A, B, out};
        for (auto c : bufs) {
            c.set_host_alignment(16)
             .dim(0).set_bounds(0, size)
             .dim(1).set_stride(size);
        }
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(MatMul, mat_mul)
