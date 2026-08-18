// The same softmax as wmma_softmax_from_memory, but narrowing to half
// precision on the way out, which is what feeds a half precision matrix
// multiply. The two accumulator layouts put an entry in the same lane as each
// other and differ only in how a lane packs what it holds, so this is a
// convert within each lane with no cross-lane traffic at all.

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

    const int W = 32, H = 32, tile = 16;

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

    soft(x, y) = cast<float16_t>(e(x, y) / total(y));
    out(x, y) = soft(x, y);

    Var xo("xo"), yo("yo"), xio("xio"), yio("yio"), xi("xi"), yi("yi");
    Var rxi("rxi"), ryi("ryi");

    // One warp owns whole rows, because the reductions run along them.
    out.bound(x, 0, W)
        .bound(y, 0, H)
        .tile(x, y, xo, yo, xi, yi, W, H)
        .tile(xi, yi, xio, yio, xi, yi, tile, tile)
        .gpu_blocks(xo, yo)
        .unroll(xio)
        .unroll(yio)
        .tile_store(xi, yi);

    for (Func f : {s, e, soft}) {
        f.compute_at(out, xo)
            .store_in(MemoryType::Tile)
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
            .split(y, y, ryi, tile)
            .unroll(y)
            .vectorize(ryi);
        f.update()
            .split(y, y, ryi, tile)
            .unroll(y)
            .tile_reduce(r, ryi);
    }

    Buffer<float16_t> result = out.realize({W, H}, target);

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
            if (std::abs((float)result(x, y) - correct) > 1e-3f * correct + 1e-4f) {
                printf("result(%d, %d) = %f instead of %f\n",
                       x, y, (float)result(x, y), correct);
                return 1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
