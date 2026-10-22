#include "Halide.h"

#include <algorithm>

using namespace Halide;

namespace {

void set_alignment_and_bounds(OutputImageParam p, int size) {
    p.set_host_alignment(16)
        .dim(0)
        .set_bounds(0, size)
        .dim(1)
        .set_stride(size);
}

// A square matrix multiply. The operands are untyped, so their type is a
// generator param, and it picks the schedule: the tensor cores multiply
// halves, bfloats and bytes, and anything else accumulates in ordinary
// registers. The output type is the accumulator type, so it picks between the
// accumulators an operand type can pair with.
//
// GFlop/s on an RTX 5060 Ti, against cublas and against the ceiling of the
// instructions used. Rows within a pair of types are comparable; rows in
// different pairs are not, because narrower operands or a narrower
// accumulator are less work.
//
//                             1024      2048      4096   ceiling
//     Halide f32             14083     16310     13764     25960
//     cublas f32             14691     16855     17709
//
//     Halide f16 -> f32      40808     46932     48387     51541
//     cublas f16 -> f32      42480     49639     51242
//
//     Halide bf16 -> f32     40854     47011     49459     51541
//     cublas bf16 -> f32     42446     49678     50981
//
//     Halide f16 -> f16      61608     80619     87447     99626
//     cublas f16 -> f16      73770     76387     90089
//
//     Halide u8 -> i32       64363     84745     90937    100650
//     cublas u8 -> i32      120895    129015    140323
//
// The tensor core ceilings are measured, by issuing wmma instructions back to
// back out of registers. The float one is 36 SMs times the 2817 MHz this part
// averages while benchmarking times the 256 flops per SM per clock the cuda
// cores do. cublas is asked for a beta of zero, so that it writes its output
// rather than accumulating onto it, which is what the filters here do.
//
// At 4096 the tensor core schedules reach 88% to 96% of their ceilings, and
// they beat cublas at f16 -> f16 at 2048. The u8 row falls short for a reason
// outside the schedule.
//
// At eight bits cublas is 39% past the ceiling, so it is not using wmma, which
// multiplies bytes no faster than it multiplies halves into halves. The mma
// instructions reach 188355 GOP/s at the same shape, 1.87x, for the same 8192
// ops per instruction. Reaching that needs mma's fragment layout, which this
// schedule cannot express.
//
class MatMul : public Halide::Generator<MatMul> {
public:
    GeneratorParam<int> size{"size", 1024};
    // For the float schedule: the tile of the output one block computes, the
    // piece of it one thread holds in registers, and how much of the reduction
    // is staged at a time.
    GeneratorParam<int> block_x{"block_x", 64};
    GeneratorParam<int> block_y{"block_y", 64};
    GeneratorParam<int> reg_x{"reg_x", 4};
    GeneratorParam<int> reg_y{"reg_y", 8};
    GeneratorParam<int> chunk{"chunk", 32};

    // How many tensor core tiles of accumulator each warp holds, and how many
    // warps per block in each dimension. Zero means use the measured shapes
    // below.
    GeneratorParam<int> tiles_x{"tiles_x", 0};
    GeneratorParam<int> tiles_y{"tiles_y", 0};
    GeneratorParam<int> warps_x{"warps_x", 0};
    GeneratorParam<int> warps_y{"warps_y", 0};
    // How much of the reduction is staged in shared memory at a time. Zero
    // means use the depth that goes with the shape below.
    GeneratorParam<int> block_r{"block_r", 0};
    // Extra elements per row of the shared panels, to spread consecutive rows
    // across banks. Zero means sixteen bytes, the least that keeps each row
    // aligned for both the widest asynchronous copy and the tensor core loads.
    // That is a different number of elements per operand type.
    GeneratorParam<int> pad_a{"pad_a", 0};
    GeneratorParam<int> pad_b{"pad_b", 0};

    Input<Buffer<void, 2>> A{"A"};
    Input<Buffer<void, 2>> B{"B"};

    // The output type is the accumulator type - there is no tensor core store
    // from a half fragment into single precision memory. A half accumulator
    // halves the registers it takes, but loses accuracy over a long reduction,
    // so only ask for it when the numerics allow.
    Output<Buffer<void, 2>> out{"out"};

    // Asking for a type the tensor cores multiply is what asks for them.
    bool use_tensor_cores() const {
        Type t = A.type();
        return t == Float(16) || t == BFloat(16) || t == Int(8) || t == UInt(8);
    }

    // The accumulator each operand type pairs with. Halves can also
    // accumulate into halves, which is what asking for a half output does.
    Type natural_accumulator() const {
        Type t = A.type();
        if (t == Int(8) || t == UInt(8)) {
            return Int(32);
        }
        return Float(32);
    }

    void generate() {
        _halide_user_assert(A.type() == B.type())
            << "The two operands must have the same type, but they are "
            << A.type() << " and " << B.type() << ".\n";
        _halide_user_assert(out.type() == natural_accumulator() ||
                            (out.type() == Float(16) && A.type() == Float(16)))
            << "A " << A.type() << " matrix multiply accumulates into "
            << natural_accumulator()
            << (A.type() == Float(16) ? " or float16" : "")
            << ", but a " << out.type() << " output was asked for.\n";
        r = RDom(0, size, "r");

        Type acc = out.type();
        prod(x, y) = cast(acc, 0);
        // The widening happens here, at the multiply, rather than in the
        // operand wrappers the schedule stages through shared memory, so that
        // narrow operands are staged narrow and reach the tensor cores so.
        prod(x, y) += cast(acc, A(x, r)) * cast(acc, B(r, y));

        out(x, y) = prod(x, y);
    }

    void schedule() {
        if (using_autoscheduler()) {
            A.dim(0).set_estimate(0, size).dim(1).set_estimate(0, size);
            B.dim(0).set_estimate(0, size).dim(1).set_estimate(0, size);
            out.bound(x, 0, size).bound(y, 0, size);
            return;
        }

        set_alignment_and_bounds(A, size);
        set_alignment_and_bounds(B, size);
        set_alignment_and_bounds(out, size);

        out.bound(x, 0, size).bound(y, 0, size);

        if (use_tensor_cores()) {
            schedule_tensor_cores();
        } else {
            schedule_cuda();
        }
    }

private:
    // See the table above.
    void schedule_cuda() {
        const int bx = block_x, by = block_y, rx = reg_x, ry = reg_y, k = chunk;
        const int tx = bx / rx, ty = by / ry;

        Var xi, yi, xii, yii;
        RVar rxo, rxi;

        // A block computes a block_x by block_y tile of the output with tx by
        // ty threads, each holding a reg_x by reg_y tile of the accumulator in
        // registers. The accumulator lives at block level so that the loop
        // over the reduction can sit above the loop over threads, which lets
        // one staged panel of each input serve every thread in the block.
        out.tile(x, y, xi, yi, bx, by)
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

        // One panel of each input per block per step of the reduction, copied
        // from global to shared by all the threads together. Each thread moves
        // four floats at a time, which is the widest asynchronous copy the
        // hardware has. Both panels are laid over the same grid of threads as
        // the compute, so that no thread sits idle in either phase.
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
    }

    // See the table above.
    void schedule_tensor_cores() {
        // Each operand tile loaded feeds tiles_x (or tiles_y) multiplies, so
        // the tile counts are what get reuse out of the loads.
        const int tile = 16;
        int tx = tiles_x, ty = tiles_y, wx = warps_x, wy = warps_y;
        // The staging depth goes with the shape below unless asked for.
        int br = 32;
        if (tx == 0 || ty == 0 || wx == 0 || wy == 0) {
            // Measured on an RTX 5060 Ti by sweeping shapes at each size and
            // operand type. There is no trend worth extrapolating from, so
            // these are measured points. The operand type matters as much as
            // the size: bytes want a tall block and a deep staged panel, where
            // the 16-bit types want a wide block and a shallow one. Bfloats pick
            // out the same shapes as halves at every size, so they share a row.
            const bool bytes = A.type().bits() == 8;
            const bool half_accumulator = out.type() == Float(16);
            if (bytes) {
                if ((int)size <= 1024) {
                    tx = 2, ty = 8, wx = 2, wy = 1, br = 64;
                } else if ((int)size <= 2048) {
                    tx = 2, ty = 10, wx = 2, wy = 1, br = 32;
                } else {
                    tx = 2, ty = 8, wx = 2, wy = 1, br = 64;
                }
            } else if (half_accumulator) {
                if ((int)size <= 1024) {
                    tx = 4, ty = 4, wx = 2, wy = 1;
                } else if ((int)size <= 2048) {
                    tx = 5, ty = 4, wx = 2, wy = 2;
                } else {
                    tx = 4, ty = 4, wx = 2, wy = 2;
                }
            } else {
                if ((int)size <= 1024) {
                    tx = 8, ty = 2, wx = 1, wy = 1;
                } else if ((int)size <= 2048) {
                    tx = 5, ty = 4, wx = 1, wy = 2;
                } else {
                    tx = 4, ty = 2, wx = 2, wy = 2;
                }
            }
        }
        if (block_r) {
            br = block_r;
        }
        const int block_w = tile * tx * wx;
        const int block_h = tile * ty * wy;

        const int pa = pad_a ? (int)pad_a : 16 / A.type().bytes();
        const int pb = pad_b ? (int)pad_b : 16 / A.type().bytes();

        // out and prod are tiled the same way: blocks of warps, each warp
        // holding several tensor core tiles. mmx, mmy and mmr are the
        // dimensions of one tensor core operation.
        Var xi("xi"), yi("yi"), xw("xw"), yw("yw");
        Var mmx("mmx"), mmy("mmy");
        RVar ro("ro"), ri("ri"), mmr("mmr");

        out.tile(x, y, xi, yi, block_w, block_h)
            .tile(xi, yi, xw, yw, xi, yi, tile * tx, tile * ty)
            .tile(xi, yi, mmx, mmy, tile, tile)
            .gpu_blocks(x, y)
            .gpu_threads(xw, yw)
            .unroll(xi)
            .unroll(yi)
            .vectorize(mmx)
            .vectorize(mmy);

        // The accumulators live in tensor core registers for the whole
        // reduction, and are written out to memory once at the end. They sit
        // at block level so that the reduction loop can be above the loop over
        // warps, which lets every warp share one staged panel.
        prod.compute_at(out, x)
            .store_in(MemoryType::Tile)
            .tile(x, y, xw, yw, xi, yi, tile * tx, tile * ty)
            .tile(xi, yi, mmx, mmy, tile, tile)
            .gpu_threads(xw, yw)
            .unroll(xi)
            .unroll(yi)
            .vectorize(mmx)
            .vectorize(mmy);

        prod.update()
            .split(r, ro, ri, br)
            .split(ri, ri, mmr, tile)
            .tile(x, y, xw, yw, xi, yi, tile * tx, tile * ty)
            .tile(xi, yi, mmx, mmy, tile, tile)
            .reorder(mmr, mmx, mmy, xi, yi, ri, xw, yw, ro)
            .gpu_threads(xw, yw)
            .unroll(xi)
            .unroll(yi)
            .unroll(ri)
            .atomic()
            .vectorize(mmr)
            .vectorize(mmx)
            .vectorize(mmy);

        // Stage the operand panels into shared memory once per reduction step,
        // to be shared by every warp in the block. Each thread moves sixteen
        // bytes at a time along the dense dimension, so that the reads from
        // global memory coalesce and the writes to shared memory can be done
        // as asynchronous copies.
        // Each thread moves sixteen bytes, the widest asynchronous copy the
        // hardware has. How many elements that is depends on the operand type.
        const int vec = 16 / A.type().bytes();
        Var ko("ko"), kv("kv"), xo("xo"), xv("xv");
        Var t("t"), ti("ti"), to("to");

        // B.in() is dense in the reduction dimension, which is its _0.
        B.in()
            .compute_at(prod, ro)
            .store_in(MemoryType::GPUSharedAsync)
            .align_storage(_0, br + pa)
            .split(_0, ko, kv, vec)
            .fuse(ko, _1, t)
            .split(t, t, ti, 32)
            .split(t, t, xw, wx)
            .split(t, to, yw, wy)
            .gpu_lanes(ti)
            .gpu_threads(xw, yw)
            .reorder(to, ti, xw, yw)
            .vectorize(kv);

        // A.in() is dense in x, which is its _0.
        A.in()
            .compute_at(prod, ro)
            .store_in(MemoryType::GPUSharedAsync)
            .align_storage(_0, block_w + pb)
            .split(_0, xo, xv, vec)
            .fuse(xo, _1, t)
            .split(t, t, ti, 32)
            .split(t, t, xw, wx)
            .split(t, to, yw, wy)
            .gpu_lanes(ti)
            .gpu_threads(xw, yw)
            .reorder(to, ti, xw, yw)
            .vectorize(xv);
    }

    Var x{"x"}, y{"y"};
    RDom r;
    Func prod{"prod"};
};

}  // namespace

HALIDE_REGISTER_GENERATOR(MatMul, mat_mul)
