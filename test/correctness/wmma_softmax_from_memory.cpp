// A softmax over a matrix that is already in memory, done entirely in tensor
// core fragments with no matrix multiply anywhere: a tile load, a reduction
// along the rows, elementwise work against the result of that, and a tile
// store. The tile is only ever loaded, worked on and stored, so the load is
// the only thing that says what shape it is.
//
// Also with more than one warp per block. The fragments then sit outside the
// loop over warps, so each warp gets its own copy of them and indexes them
// with its own warp number. That only picks between the copies - within one, a
// warp names the same entry as every other - so the reads still make sense.

#include "Halide.h"
#include <cstdio>

using namespace Halide;

namespace {

bool test(int warps) {
    const int tile = 16, W = 32, H = tile * 2 * warps;

    Buffer<float> scores(W, H);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            scores(x, y) = (float)(rand() & 7);
        }
    }

    Var x("x"), y("y");
    RDom r(0, W, "r");
    Func s("s"), m("m"), e("e"), total("total"), soft("soft"), out("out");

    s(x, y) = scores(x, y);

    m(y) = -1e30f;
    m(y) = max(m(y), s(r, y));

    e(x, y) = exp(s(x, y) - m(y));

    total(y) = 0.f;
    total(y) += e(r, y);

    soft(x, y) = e(x, y) / total(y);
    out(x, y) = soft(x, y);

    Var xo("xo"), yo("yo"), xio("xio"), yio("yio"), xi("xi"), yi("yi");
    Var yw("yw"), rxi("rxi"), ryi("ryi");

    // A warp owns whole rows, because the reductions run along them, and a
    // block is some number of warps' worth of rows.
    const int rows = H / warps;
    out.bound(x, 0, W)
        .bound(y, 0, H)
        .tile(x, y, xo, yo, xi, yi, W, H)
        .split(yi, yw, yi, rows)
        .tile(xi, yi, xio, yio, xi, yi, tile, tile)
        .gpu_blocks(xo, yo)
        .unroll(xio)
        .unroll(yio)
        .tile_store(xi, yi);

    for (Func f : {s, e, soft}) {
        f.compute_at(out, xo)
            .store_in(MemoryType::Tile)
            .split(y, yw, y, rows)
            .tile(x, y, rxi, ryi, tile, tile)
            .unroll(x)
            .unroll(y);
    }
    s.tile_load(rxi, ryi);
    e.tile_init(rxi, ryi);
    soft.tile_init(rxi, ryi);

    for (Func f : {m, total}) {
        f.store_in(MemoryType::Tile)
            .compute_at(out, xo)
            .split(y, yw, y, rows)
            .split(y, y, ryi, tile)
            .unroll(y)
            .vectorize(ryi);
        f.update()
            .split(y, yw, y, rows)
            .split(y, y, ryi, tile)
            .unroll(y)
            .tile_reduce(r, ryi);
    }

    if (warps > 1) {
        // With one warp there is no loop over warps to put anything in.
        out.gpu_threads(yw);
        for (Func f : {s, e, soft, m, total}) {
            f.gpu_threads(yw);
        }
        for (Func f : {m, total}) {
            f.update().gpu_threads(yw);
        }
    }

    Buffer<float> result = out.realize({W, H}, get_jit_target_from_environment());

    for (int y = 0; y < H; y++) {
        float row_max = -1e30f;
        for (int x = 0; x < W; x++) {
            row_max = std::max(row_max, scores(x, y));
        }
        float sum = 0;
        for (int x = 0; x < W; x++) {
            sum += std::exp(scores(x, y) - row_max);
        }
        for (int x = 0; x < W; x++) {
            float correct = std::exp(scores(x, y) - row_max) / sum;
            if (std::abs(result(x, y) - correct) > 1e-6f) {
                printf("With %d warps per block, result(%d, %d) = %f instead of %f\n",
                       warps, x, y, result(x, y), correct);
                return false;
            }
        }
    }
    return true;
}

}  // namespace

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (!target.has_feature(Target::CUDA)) {
        printf("[SKIP] WMMA operations require CUDA.\n");
        return 0;
    }
    if (target.get_cuda_capability_lower_bound() < 70) {
        printf("[SKIP] WMMA operations require CUDA compute capability 7.0 or above.\n");
        return 0;
    }

    for (int warps : {1, 2, 4}) {
        if (!test(warps)) {
            printf("Failed!\n");
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
