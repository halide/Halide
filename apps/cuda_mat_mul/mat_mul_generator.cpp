#include "Halide.h"

using namespace Halide;

namespace {

class MatMul : public Halide::Generator<MatMul> {
public:

    GeneratorParam<int>   size {"size", 1024};
    ImageParam            A {Float(32), 2, "A"};
    ImageParam            B {Float(32), 2, "B"};

    Func build() {
        Var x("x"), y("y");

        Func prod("prod");
        RDom r(0, size);
        prod(x, y) += A(x, r) * B(r, y);

        Var xi, yi, xio, xii, yii, xo;
        Func out = prod.in();
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

        OutputImageParam bufs[] = {A, B, prod.output_buffer()};
        for (auto &buf : bufs) {
            buf.set_host_alignment(16)
                .dim(0).set_bounds(0, size)
                .dim(1).set_stride(size);
        }

        return out;
    }
};

Halide::RegisterGenerator<MatMul> register_me{"mat_mul"};
}
