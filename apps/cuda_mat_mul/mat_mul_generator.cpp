#include "Halide.h"

namespace {

class MatMul : public Halide::Generator<MatMul> {
public:

    GeneratorParam<int>   size {"size", 1024};
    ImageParam            A {Float(32), 2, "A"};
    ImageParam            B {Float(32), 2, "B"};

    Func build() {
        Var x, y;

        Func prod("prod");
        RDom r(0, size);
        prod(x, y) += A(x, r) * B(r, y);

        Var xi, yi, xii, yii;
        Func out = prod.in();
        out.bound(x, 0, size)
            .bound(y, 0, size)
            .tile(x, y, xi, yi, 16, 8)
            .vectorize(xi, 4)
            .unroll(xi)
            .unroll(yi)
            .gpu_tile(x, y, 8, 8);
        prod.compute_at(out, Var::gpu_threads())
            .unroll(x)
            .unroll(y)
            .update()
            .reorder(x, y, r.x)
            .tile(x, y, xi, yi, 2, 2)
            .vectorize(xi)
            .unroll(yi)
            .tile(x, y, xii, yii, 2, 2)
            .unroll(xii)
            .unroll(yii)
            .unroll(x)
            .unroll(y);

        Halide::OutputImageParam bufs[] = {A, B, out.output_buffer()};
        for (auto &buf : bufs) {
            buf.set_host_alignment(16)
                .set_bounds(0, 0, size)
                .set_stride(1, size);
        }

        return out;
    }
};

Halide::RegisterGenerator<MatMul> register_me{"mat_mul"};
}
