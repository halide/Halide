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

// A square matrix multiply, scheduled two ways. The operands are untyped, so
// their type is a generator param, and it is what picks the schedule: half
// precision operands get the tensor cores, and anything else gets a schedule
// that keeps the accumulator in ordinary registers. The product is always
// accumulated and returned in single precision.
class MatMul : public Halide::Generator<MatMul> {
public:
    GeneratorParam<int> size{"size", 1024};

    // How many tensor core tiles of accumulator each warp holds, and how many
    // warps there are per block in each dimension. Zero means pick a shape
    // based on the problem size. The best block gets smaller as the matrices
    // do, because a large one leaves too few blocks to fill the machine.
    GeneratorParam<int> tiles_x{"tiles_x", 0};
    GeneratorParam<int> tiles_y{"tiles_y", 0};
    GeneratorParam<int> warps_x{"warps_x", 0};
    GeneratorParam<int> warps_y{"warps_y", 0};
    // How much of the reduction is staged in shared memory at a time.
    GeneratorParam<int> block_r{"block_r", 32};
    // Extra elements per row of the shared panels, which spreads consecutive
    // rows across different banks. A multiple of eight keeps the rows aligned
    // enough for the widest asynchronous copy.
    GeneratorParam<int> pad_a{"pad_a", 8};
    GeneratorParam<int> pad_b{"pad_b", 24};

    Input<Buffer<void, 2>> A{"A"};
    Input<Buffer<void, 2>> B{"B"};

    Output<Buffer<float, 2>> out{"out"};

    // Tensor cores multiply half precision operands into a single precision
    // accumulator, so asking for half precision inputs is what asks for them.
    bool use_tensor_cores() const {
        return A.type() == Float(16);
    }

    void generate() {
        r = RDom(0, size, "r");

        // Wrappers for the operands, so that the tensor core schedule can
        // stage them through shared memory. Left inline, they are just A
        // and B. They keep the operand type, so that half precision operands
        // are staged as half precision and reach the tensor cores as such -
        // the widening to the accumulator type happens at the multiply.
        As(x, rr) = A(x, rr);
        Bs(rr, y) = B(rr, y);

        prod(x, y) = 0.f;
        prod(x, y) += cast<float>(As(x, r)) * cast<float>(Bs(r, y));

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
    // 688 us for 1024x1024 floats on an RTX 2060, where cublas is 512 us.
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

        As.compute_at(prod, r).vectorize(x).unroll(rr);
        Bs.compute_at(prod, r).vectorize(rr).unroll(y);
    }

    void schedule_tensor_cores() {
        // The tensor core tile shape, and how many of them each warp
        // accumulates at once. Each operand tile loaded feeds tiles_x (or
        // tiles_y) multiplies, so this is what gets us reuse out of the loads.
        const int tile = 16;
        int tx = tiles_x, ty = tiles_y, wx = warps_x, wy = warps_y;
        int pb = pad_b;
        if (tx == 0 || ty == 0 || wx == 0 || wy == 0) {
            // The padding goes with the shape: it is what keeps consecutive
            // rows of the staged panel in different banks, so the right amount
            // depends on how wide the panel is.
            if ((int)size <= 1024) {
                // Small problems need small blocks: a 160x64 block leaves only
                // a few dozen of them to cover 36 SMs, and 160 does not divide
                // 1024 so the last one in each row is ragged.
                tx = 8, ty = 2, wx = 1, wy = 1, pb = 8;
            } else {
                tx = 5, ty = 4, wx = 2, wy = 1, pb = 24;
            }
        }
        const int block_x = tile * tx * wx;
        const int block_y = tile * ty * wy;

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
            .split(r, ro, ri, block_r)
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
        const int vec = 8;
        Var rro("rro"), rrv("rrv"), xxo("xxo"), xxi("xxi");
        Var t("t"), ti("ti"), tw("tw"), tw2("tw2"), to("to");

        // Bs is dense in the reduction dimension.
        Bs.compute_at(prod, ro)
            .store_in(MemoryType::GPUSharedAsync)
            .align_storage(rr, (int)block_r + (int)pad_a)
            .split(rr, rro, rrv, vec)
            .fuse(rro, y, t)
            .split(t, t, ti, 32)
            .split(t, t, tw, wx)
            .split(t, to, tw2, wy)
            .gpu_lanes(ti)
            .gpu_threads(tw, tw2)
            .vectorize(rrv);

        // As is dense in x.
        As.compute_at(prod, ro)
            .store_in(MemoryType::GPUSharedAsync)
            .align_storage(x, block_x + pb)
            .split(x, xxo, xxi, vec)
            .fuse(xxo, rr, t)
            .split(t, t, ti, 32)
            .split(t, t, tw, wx)
            .split(t, to, tw2, wy)
            .gpu_lanes(ti)
            .gpu_threads(tw, tw2)
            .vectorize(xxi);
    }

    Var x{"x"}, y{"y"}, rr{"rr"};
    RDom r;
    Func prod{"prod"}, As{"As"}, Bs{"Bs"};
};

}  // namespace

HALIDE_REGISTER_GENERATOR(MatMul, mat_mul)
