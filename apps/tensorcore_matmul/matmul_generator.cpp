#include "Halide.h"

namespace {

using namespace Halide;

enum class Schedule {
    CUDA,
    TensorCore,
};

class MatMul : public Halide::Generator<MatMul> {
public:
    GeneratorParam<Schedule> gpu_schedule{
        "gpu_schedule", Schedule::TensorCore,
        {{"cudaonly", Schedule::CUDA}, {"tensorcore", Schedule::TensorCore}}};

    GeneratorParam<int> M{"M", 1024};
    GeneratorParam<int> N{"N", 1024};
    GeneratorParam<int> K{"K", 1024};

    // How many tensor core tiles of accumulator each warp holds, and how many
    // warps there are per block.
    GeneratorParam<int> tiles_x{"tiles_x", 5};
    GeneratorParam<int> tiles_y{"tiles_y", 4};
    GeneratorParam<int> warps{"warps", 4};

    Input<Buffer<float16_t, 2>> matA{"matA"};  // K x M
    Input<Buffer<float16_t, 2>> matB{"matB"};  // N x K

    Output<Buffer<float, 2>> output{"output"};

    void generate() {
        k = RDom(0, K, "k");

        prod(x, y) = 0.f;
        prod(x, y) += cast<float>(matA(k, y)) * cast<float>(matB(x, k));

        output(x, y) = prod(x, y);
    }

    void schedule() {
        matA.dim(0).set_bounds(0, K).set_stride(1);
        matA.dim(1).set_bounds(0, M).set_stride(K);
        matB.dim(0).set_bounds(0, N).set_stride(1);
        matB.dim(1).set_bounds(0, K).set_stride(N);
        output.dim(0).set_bounds(0, N).set_stride(1);
        output.dim(1).set_bounds(0, M).set_stride(N);

        if (gpu_schedule == Schedule::CUDA) {
            // Schedule taken from the cuda_mat_mul app.
            Var xi, yi, xii, yii;

            output.bound(x, 0, N)
                .bound(y, 0, M)
                .tile(x, y, xi, yi, 64, 16)
                .tile(xi, yi, xii, yii, 4, 8)
                .gpu_blocks(x, y)
                .gpu_threads(xi, yi)
                .unroll(xii)
                .unroll(yii);

            prod.compute_at(output, xi)
                .vectorize(x)
                .unroll(y)
                .update()
                .reorder(x, y, k)
                .vectorize(x)
                .unroll(y)
                .unroll(k, 8);

            matA.in().compute_at(prod, k).vectorize(_0).unroll(_1);
            matB.in().compute_at(prod, k).vectorize(_0).unroll(_1);
        } else {
            // The tensor core tile shape, and how many of them each warp
            // accumulates at once. Each operand tile loaded feeds tiles_x (or
            // tiles_y) multiplies, so this is what gets us reuse out of the
            // loads.
            const int tile_x = 16, tile_y = 16, tile_k = 16;

            Var xi("xi"), yi("yi"), xt("xt"), mmxi("mmxi"), mmyi("mmyi");
            Var rxi("rxi"), ryi("ryi");
            RVar rro("rro"), rri("rri");

            output.bound(x, 0, N)
                .bound(y, 0, M)
                .split(x, x, xi, tile_x * tiles_x * warps)
                .split(xi, xt, xi, tile_x * tiles_x)
                .split(xi, xi, mmxi, tile_x)
                .split(y, y, yi, tile_y * tiles_y)
                .split(yi, yi, mmyi, tile_y)
                .gpu_blocks(x, y)
                .gpu_threads(xt)
                .reorder(mmxi, mmyi, xi, yi, xt, x, y)
                .unroll(xi)
                .unroll(yi)
                .vectorize(mmxi)
                .vectorize(mmyi);

            // The accumulators live in tensor core registers for the whole
            // reduction, and are written out to memory once at the end.
            prod.compute_at(output, xt)
                .store_in(MemoryType::WMMAAccumulator)
                .split(x, x, rxi, tile_x)
                .split(y, y, ryi, tile_y)
                .vectorize(rxi)
                .vectorize(ryi)
                .unroll(x)
                .unroll(y);

            prod.update()
                .split(x, x, rxi, tile_x)
                .split(y, y, ryi, tile_y)
                .split(k, rro, rri, tile_k)
                .reorder(rri, rxi, ryi, x, y, rro)
                .unroll(x)
                .unroll(y)
                .atomic()
                .vectorize(rri)
                .vectorize(rxi)
                .vectorize(ryi);
        }
    }

private:
    Var x{"x"}, y{"y"};
    RDom k;
    Func prod{"prod"};
};

}  // namespace

HALIDE_REGISTER_GENERATOR(MatMul, matmul)
