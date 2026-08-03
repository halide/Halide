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
// halves, brain floats and bytes, and anything else accumulates in ordinary
// registers. The output type is the accumulator type, so it picks between the
// accumulators an operand type can pair with.
//
// GFlop/s on an RTX 5060 Ti, against cublas and against the ceiling of the
// instructions used. Rows within a pair of types are comparable; rows in
// different pairs are not, because narrower operands or a narrower
// accumulator are less work.
//
//                             1024      2048      4096   ceiling
//     Halide f32              6878     10294      7298     25960
//     cublas f32             14503     16574     17449
//
//     Halide f16 -> f32      40070     46671     48888     51541
//     cublas f16 -> f32      41658     49069     50440
//
//     Halide bf16 -> f32     40025     46681     48692     51541
//     cublas bf16 -> f32     41664     49054     50437
//
//     Halide f16 -> f16      60349     86502     86599     99626
//     cublas f16 -> f16      69221     75073     87073
//
//     Halide u8 -> i32       62869     82391     89641    100650
//     cublas s8 -> i32      107868    122203    129970
//
// The tensor core ceilings are measured, by issuing wmma instructions back to
// back out of registers. The float one is 36 SMs times the 2817 MHz this part
// averages while benchmarking times the 256 flops per SM per clock the cuda
// cores do.
//
// The tensor core schedules reach 87% to 95% of their ceilings, and match
// cublas at f16 -> f16. Two rows fall short for reasons outside the schedule.
//
// At eight bits cublas is 29% past the ceiling, so it is not using wmma, which
// multiplies bytes no faster than it multiplies halves into halves. The mma
// instructions reach 188355 GOP/s at the same shape, 1.87x, for the same 8192
// ops per instruction. Reaching that needs mma's fragment layout, which this
// schedule cannot express.
//
// The float row is at 28% because sharing a staged panel across a block needs
// the reduction chunk loop above the thread loops and the accumulator below
// them, living across it. Such an accumulator lands at block level, where a
// Register allocation is sized for the whole block tile and spills. So it
// stages per thread and shares nothing. The tensor core schedules only escape
// this because a WMMAFragment allocation at block level is already per-lane.
//
class MatMul : public Halide::Generator<MatMul> {
public:
    GeneratorParam<int> size{"size", 1024};

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
        Var xi, yi, xii, yii;

        out.tile(x, y, xi, yi, 64, 16)
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
            // the 16-bit types want a wide block and a shallow one. Brain
            // floats pick out the same shapes as halves at every size, so they
            // share a row.
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
        const int block_x = tile * tx * wx;
        const int block_y = tile * ty * wy;

        const int pa = pad_a ? (int)pad_a : 16 / A.type().bytes();
        const int pb = pad_b ? (int)pad_b : 16 / A.type().bytes();

        Var xi("xi"), yi("yi"), xt("xt"), yt("yt"), mmxi("mmxi"), mmyi("mmyi");
        Var xw("xw"), yw("yw"), rxi("rxi"), ryi("ryi");
        RVar ro("ro"), ri("ri"), rri("rri");

        out.split(x, x, xi, block_x)
            .split(xi, xt, xi, tile * tx)
            .split(xi, xi, mmxi, tile)
            .split(y, y, yi, block_y)
            .split(yi, yt, yi, tile * ty)
            .split(yi, yi, mmyi, tile)
            .gpu_blocks(x, y)
            .gpu_threads(xt, yt)
            .reorder(mmxi, mmyi, xi, yi, xt, yt, x, y)
            .unroll(xi)
            .unroll(yi)
            .vectorize(mmxi)
            .vectorize(mmyi);

        // The accumulators live in tensor core registers for the whole
        // reduction, and are written out to memory once at the end. They sit
        // at block level so that the reduction loop can be above the loop over
        // warps, which lets every warp share one staged panel.
        prod.compute_at(out, x)
            .store_in(MemoryType::WMMAFragment)
            .split(x, xw, xi, tile * tx)
            .split(xi, xi, rxi, tile)
            .split(y, yw, yi, tile * ty)
            .split(yi, yi, ryi, tile)
            .reorder(rxi, ryi, xi, yi, xw, yw)
            .gpu_threads(xw, yw)
            .vectorize(rxi)
            .vectorize(ryi)
            .unroll(xi)
            .unroll(yi);

        prod.update()
            .split(r, ro, ri, br)
            .split(x, xw, xi, tile * tx)
            .split(xi, xi, rxi, tile)
            .split(y, yw, yi, tile * ty)
            .split(yi, yi, ryi, tile)
            .split(ri, ri, rri, tile)
            .reorder(rri, rxi, ryi, xi, yi, ri, xw, yw, ro)
            .gpu_threads(xw, yw)
            .unroll(xi)
            .unroll(yi)
            .unroll(ri)
            .atomic()
            .vectorize(rri)
            .vectorize(rxi)
            .vectorize(ryi);

        // Stage the operand panels into shared memory once per reduction step,
        // to be shared by every warp in the block. Each thread moves sixteen
        // bytes at a time along the dense dimension, so that the reads from
        // global memory coalesce and the writes to shared memory can be done
        // as asynchronous copies.
        // Each thread moves sixteen bytes, the widest asynchronous copy the
        // hardware has. How many elements that is depends on the operand type.
        const int vec = 16 / A.type().bytes();
        Var rro("rro"), rrv("rrv"), xxo("xxo"), xxi("xxi");
        Var t("t"), ti("ti"), tw("tw"), tw2("tw2"), to("to");

        // B.in() is dense in the reduction dimension, which is its _0.
        B.in().compute_at(prod, ro).store_in(MemoryType::GPUSharedAsync).align_storage(_0, br + pa).split(_0, rro, rrv, vec).fuse(rro, _1, t).split(t, t, ti, 32).split(t, t, tw, wx).split(t, to, tw2, wy).gpu_lanes(ti).gpu_threads(tw, tw2).vectorize(rrv);

        // A.in() is dense in x, which is its _0.
        A.in().compute_at(prod, ro).store_in(MemoryType::GPUSharedAsync).align_storage(_0, block_x + pb).split(_0, xxo, xxi, vec).fuse(xxo, _1, t).split(t, t, ti, 32).split(t, t, tw, wx).split(t, to, tw2, wy).gpu_lanes(ti).gpu_threads(tw, tw2).vectorize(xxi);
    }

    Var x{"x"}, y{"y"};
    RDom r;
    Func prod{"prod"};
};

}  // namespace

HALIDE_REGISTER_GENERATOR(MatMul, mat_mul)
