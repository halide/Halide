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
    // warps there are per block in each dimension.
    GeneratorParam<int> tiles_x{"tiles_x", 5};
    GeneratorParam<int> tiles_y{"tiles_y", 4};
    GeneratorParam<int> warps_x{"warps_x", 2};
    GeneratorParam<int> warps_y{"warps_y", 1};
    // How much of the reduction is staged in shared memory at a time.
    GeneratorParam<int> block_k{"block_k", 32};
    // Extra elements per row of the shared panels, which spreads consecutive
    // rows across different banks. A multiple of eight keeps the rows aligned
    // enough for the widest asynchronous copy.
    GeneratorParam<int> pad_a{"pad_a", 8};
    GeneratorParam<int> pad_b{"pad_b", 16};

    Input<Buffer<float16_t, 2>> matA{"matA"};  // K x M
    Input<Buffer<float16_t, 2>> matB{"matB"};  // N x K

    Output<Buffer<float, 2>> output{"output"};

    void generate() {
        k = RDom(0, K, "k");

        // Wrappers for the operands, so that the tensor core schedule can
        // stage them through shared memory. Left inline, they are just matA
        // and matB.
        As(kk, y) = matA(kk, y);
        Bs(x, kk) = matB(x, kk);

        prod(x, y) = 0.f;
        prod(x, y) += cast<float>(As(k, y)) * cast<float>(Bs(x, k));

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
            const int tile = 16;
            const int block_x = tile * tiles_x * warps_x;
            const int block_y = tile * tiles_y * warps_y;

            Var xi("xi"), yi("yi"), xt("xt"), yt("yt"), mmxi("mmxi"), mmyi("mmyi");
            Var xw("xw"), yw("yw"), rxi("rxi"), ryi("ryi");
            RVar ko("ko"), ki("ki"), rri("rri");

            output.bound(x, 0, N)
                .bound(y, 0, M)
                .split(x, x, xi, block_x)
                .split(xi, xt, xi, tile * tiles_x)
                .split(xi, xi, mmxi, tile)
                .split(y, y, yi, block_y)
                .split(yi, yt, yi, tile * tiles_y)
                .split(yi, yi, mmyi, tile)
                .gpu_blocks(x, y)
                .gpu_threads(xt, yt)
                .reorder(mmxi, mmyi, xi, yi, xt, yt, x, y)
                .unroll(xi)
                .unroll(yi)
                .vectorize(mmxi)
                .vectorize(mmyi);

            // The accumulators live in tensor core registers for the whole
            // reduction, and are written out to memory once at the end. They
            // sit at block level so that the reduction loop can be above the
            // loop over warps, which lets every warp share one staged panel.
            prod.compute_at(output, x)
                .store_in(MemoryType::WMMAFragment)
                .split(x, xw, xi, tile * tiles_x)
                .split(xi, xi, rxi, tile)
                .split(y, yw, yi, tile * tiles_y)
                .split(yi, yi, ryi, tile)
                .reorder(rxi, ryi, xi, yi, xw, yw)
                .gpu_threads(xw, yw)
                .vectorize(rxi)
                .vectorize(ryi)
                .unroll(xi)
                .unroll(yi);

            prod.update()
                .split(k, ko, ki, block_k)
                .split(x, xw, xi, tile * tiles_x)
                .split(xi, xi, rxi, tile)
                .split(y, yw, yi, tile * tiles_y)
                .split(yi, yi, ryi, tile)
                .split(ki, ki, rri, tile)
                .reorder(rri, rxi, ryi, xi, yi, ki, xw, yw, ko)
                .gpu_threads(xw, yw)
                .unroll(xi)
                .unroll(yi)
                .unroll(ki)
                .atomic()
                .vectorize(rri)
                .vectorize(rxi)
                .vectorize(ryi);

            // Stage the operand panels into shared memory once per reduction
            // step, to be shared by every warp in the block. Each thread moves
            // sixteen bytes at a time along the dense dimension, so that the
            // reads from global memory coalesce and the writes to shared
            // memory can be done as asynchronous copies.
            const int vec = 8;
            Var kko("kko"), kki("kki"), xxo("xxo"), xxi("xxi");
            Var t("t"), ti("ti"), tw("tw"), tw2("tw2"), to("to");

            As.compute_at(prod, ko)
                .store_in(MemoryType::GPUShared)
                .align_storage(kk, (int)block_k + (int)pad_a)
                .split(kk, kko, kki, vec)
                .fuse(kko, y, t)
                .split(t, t, ti, 32)
                .split(t, t, tw, warps_x)
                .split(t, to, tw2, warps_y)
                .gpu_lanes(ti)
                .gpu_threads(tw, tw2)
                .vectorize(kki);

            Bs.compute_at(prod, ko)
                .store_in(MemoryType::GPUShared)
                .align_storage(x, block_x + (int)pad_b)
                .split(x, xxo, xxi, vec)
                .fuse(xxo, kk, t)
                .split(t, t, ti, 32)
                .split(t, t, tw, warps_x)
                .split(t, to, tw2, warps_y)
                .gpu_lanes(ti)
                .gpu_threads(tw, tw2)
                .vectorize(xxi);
        }
    }

private:
    Var x{"x"}, y{"y"}, kk{"kk"};
    RDom k;
    Func prod{"prod"}, As{"As"}, Bs{"Bs"};
};

}  // namespace

HALIDE_REGISTER_GENERATOR(MatMul, matmul)
