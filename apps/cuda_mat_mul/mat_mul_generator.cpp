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
    // The tile of the output one block computes, the piece of it one thread
    // holds in registers, and how much of the reduction is staged at a time.
    GeneratorParam<int> block_x{"block_x", 64};
    GeneratorParam<int> block_y{"block_y", 64};
    GeneratorParam<int> reg_x{"reg_x", 4};
    GeneratorParam<int> reg_y{"reg_y", 8};
    GeneratorParam<int> chunk{"chunk", 32};
    Input<Buffer<float, 2>> A{"A"};
    Input<Buffer<float, 2>> B{"B"};

    Output<Buffer<float, 2>> out{"out"};

    void generate() {
        // 162 us on an RTX 5060 Ti
        // cublas is 150 us on the same card

        Var x("x"), y("y"), p("p");

        Func prod("prod");
        RDom r(0, size);
        prod(x, y) += A(x, r) * B(r, y);
        out(x, y) = prod(x, y);

        Var xi, yi, xio, xii, yii, xo, yo, x_pair, xiio, ty;
        RVar rxo, rxi;

        if (!using_autoscheduler()) {
            const int bx = block_x, by = block_y, rx = reg_x, ry = reg_y, k = chunk;
            const int tx = bx / rx, ty = by / ry;

            // A block computes a block_x by block_y tile of the output with
            // tx by ty threads, each holding a reg_x by reg_y tile of the
            // accumulator in registers. The accumulator lives at block level
            // so that the loop over the reduction can sit above the loop over
            // threads, which lets one staged panel of each input serve every
            // thread in the block.
            out.bound(x, 0, size)
                .bound(y, 0, size)
                .tile(x, y, xi, yi, bx, by)
                .tile(xi, yi, xii, yii, rx, ry)
                .gpu_blocks(x, y)
                .gpu_threads(xi, yi)
                .vectorize(xii)
                .unroll(yii);

            prod.compute_at(out, x)
                .store_in(MemoryType::Register)
                .tile(x, y, xii, yii, rx, ry)
                .gpu_threads(x, y)
                .unroll(xii)
                .unroll(yii);

            prod.update()
                .split(r, rxo, rxi, k)
                .tile(x, y, xii, yii, rx, ry)
                .reorder(xii, yii, rxi, x, y, rxo)
                .gpu_threads(x, y)
                .unroll(xii)
                .unroll(yii)
                .unroll(rxi);

            prod.in().compute_at(out, xi).unroll(x).unroll(y);

            // One panel of each input per block per step of the reduction,
            // copied from global to shared by all the threads together. Each
            // thread moves four floats at a time, which is the widest
            // asynchronous copy the hardware has. Both panels are laid over
            // the same grid of threads as the compute, so that no thread sits
            // idle in either phase.
            Var v("v"), t("t"), ti("ti"), tj("tj"), to("to");
            auto stage = [&](Func f) {
                f.compute_at(prod, rxo)
                    .store_in(MemoryType::GPUSharedAsync)
                    .split(_0, _0, v, 4)
                    .fuse(_0, _1, t)
                    .split(t, t, ti, tx)
                    .split(t, to, tj, ty)
                    .gpu_threads(ti, tj)
                    .reorder(to, ti, tj)
                    .unroll(to)
                    .vectorize(v);
            };
            stage(A.in());
            stage(B.in());
            A.in().compute_with(B.in(), ti);

            set_alignment_and_bounds(A, size);
            set_alignment_and_bounds(B, size);
            set_alignment_and_bounds(out, size);
        } else {
            A.dim(0).set_estimate(0, size).dim(1).set_estimate(0, size);
            B.dim(0).set_estimate(0, size).dim(1).set_estimate(0, size);
        }

        // Always specify bounds for outputs, whether autoscheduled or not
        out
            .bound(x, 0, size)
            .bound(y, 0, size);
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(MatMul, mat_mul)
