// A reduction along the rows of a half precision tile. A half precision
// accumulator packs two entries into each register, so a step of the butterfly
// that exchanges entries along an axis has three forms rather than two: the
// two entries a register holds differ in the low bit of the column, so
// flipping that bit permutes the halves of a register and moves nothing, while
// flipping the others is a move between registers or a shuffle between lanes,
// as it is in single precision.

#include "Halide.h"
#include <cstdio>

using namespace Halide;

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

    const int W = 16, H = 16, tile = 16;

    Buffer<float16_t> in(W, H);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            // Small integers, so the reduction is exact in half precision.
            in(x, y) = float16_t((float)((x * 7 + y * 13) % 29));
        }
    }

    Var x("x"), y("y");
    RDom r(0, W, "r");
    Func s("s"), m("m"), spread("spread"), out("out");

    s(x, y) = in(x, y);

    m(y) = float16_t(-1e4f);
    m(y) = max(m(y), s(r, y));

    // Every entry holds its row's maximum, which is how a fragment spread
    // along an axis is held, so reading it back into a whole tile costs
    // nothing.
    spread(x, y) = m(y);
    out(x, y) = spread(x, y);

    Var xo("xo"), yo("yo"), xio("xio"), yio("yio"), xi("xi"), yi("yi");
    Var rxi("rxi"), ryi("ryi");

    out.bound(x, 0, W)
        .bound(y, 0, H)
        .tile(x, y, xo, yo, xi, yi, W, H)
        .tile(xi, yi, xio, yio, xi, yi, tile, tile)
        .gpu_blocks(xo, yo)
        .unroll(xio)
        .unroll(yio)
        .tile_store(xi, yi);

    spread.compute_at(out, xo)
        .store_in(MemoryType::Tile)
        .tile(x, y, rxi, ryi, tile, tile)
        .unroll(x)
        .unroll(y)
        .tile_init(rxi, ryi);

    s.compute_at(out, xo)
        .store_in(MemoryType::Tile)
        .tile(x, y, rxi, ryi, tile, tile)
        .unroll(x)
        .unroll(y)
        .tile_load(rxi, ryi);

    m.store_in(MemoryType::Tile)
        .compute_at(out, xo)
        .split(y, y, ryi, tile)
        .unroll(y)
        .vectorize(ryi);
    m.update()
        .split(y, y, ryi, tile)
        .unroll(y)
        .tile_reduce(r, ryi);

    Buffer<float16_t> result = out.realize({W, H}, target);

    for (int y = 0; y < H; y++) {
        float correct = -1e4f;
        for (int x = 0; x < W; x++) {
            correct = std::max(correct, (float)in(x, y));
        }
        for (int x = 0; x < W; x++) {
            if ((float)result(x, y) != correct) {
                printf("result(%d, %d) = %f instead of %f\n",
                       x, y, (float)result(x, y), correct);
                return 1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
