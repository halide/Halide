// A whole softmax over the rows of a half precision matrix in memory, done
// entirely in tiles: a matrix load, a reduction along the rows, elementwise
// work against the result of that, another reduction, and a matrix store.
// There is no matrix multiply anywhere, and nothing leaves the fragments in
// between.

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
    Func s("s"), m("m"), e("e"), total("total"), soft("soft"), out("out");

    s(x, y) = in(x, y);

    m(y) = float16_t(-1e4f);
    m(y) = max(m(y), s(r, y));

    // The row statistics are held spread along the row, which is how a
    // reduction along an axis leaves them, so reading them back alongside the
    // tile they came from costs nothing.
    e(x, y) = exp(s(x, y) - m(y));

    total(y) = float16_t(0.f);
    total(y) += e(r, y);

    soft(x, y) = e(x, y) / total(y);
    out(x, y) = soft(x, y);

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

    for (Func f : {e, soft}) {
        f.compute_at(out, xo)
            .store_in(MemoryType::Tile)
            .tile(x, y, rxi, ryi, tile, tile)
            .unroll(x)
            .unroll(y)
            .tile_init(rxi, ryi);
    }

    s.compute_at(out, xo)
        .store_in(MemoryType::Tile)
        .tile(x, y, rxi, ryi, tile, tile)
        .unroll(x)
        .unroll(y)
        .tile_load(rxi, ryi);

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
        float row_max = -1e4f;
        for (int x = 0; x < W; x++) {
            row_max = std::max(row_max, (float)in(x, y));
        }
        float sum = 0;
        for (int x = 0; x < W; x++) {
            sum += std::exp((float)in(x, y) - row_max);
        }
        for (int x = 0; x < W; x++) {
            float correct = std::exp((float)in(x, y) - row_max) / sum;
            // Half precision throughout, so the tolerance is generous.
            if (std::abs((float)result(x, y) - correct) > 5e-3f * correct + 1e-3f) {
                printf("result(%d, %d) = %f instead of %f\n",
                       x, y, (float)result(x, y), correct);
                return 1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
