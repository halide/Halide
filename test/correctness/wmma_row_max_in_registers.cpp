// The same reduction as wmma_row_max, but with the tile never leaving the
// registers it was computed in. Each row is spread across four lanes of the
// warp, so every entry of it comes back through a shuffle.

#include "Halide.h"
#include <cstdio>

using namespace Halide;

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (!target.has_feature(Target::CUDA)) {
        printf("[SKIP] WMMA matrix multiplies require CUDA.\n");
        return 0;
    }
    if (target.get_cuda_capability_lower_bound() < 70) {
        printf("[SKIP] WMMA matrix multiplies require CUDA compute capability 7.0 or above.\n");
        return 0;
    }

    const int M = 16, N = 16, K = 16;
    const int tile = 16;

    Buffer<float16_t> A(K, M), B(N, K);
    for (int y = 0; y < M; y++) {
        for (int x = 0; x < K; x++) {
            A(x, y) = float16_t((float)(rand() & 3));
        }
    }
    for (int y = 0; y < K; y++) {
        for (int x = 0; x < N; x++) {
            B(x, y) = float16_t((float)(rand() & 3));
        }
    }

    Var x("x"), y("y");
    RDom k(0, K, "k");
    RDom r(0, N, "r");
    Func prod("prod"), row_max("row_max");

    prod(x, y) = 0.f;
    prod(x, y) += cast<float>(A(k, y)) * cast<float>(B(x, k));

    row_max(y) = -1e30f;
    row_max(y) = max(row_max(y), prod(r, y));

    Var yo("yo"), yi("yi"), yt("yt"), rxi("rxi"), ryi("ryi");
    RVar rro("rro"), rri("rri");

    // One warp owns the whole tile, and reduces every row of it in place.
    for (int stage = 0; stage < 2; stage++) {
        Stage s = stage ? row_max.update() : Stage(row_max);
        s.split(y, yo, yi, tile)
            .split(yo, yo, yt, 1)
            .reorder(yi, yt, yo)
            .gpu_blocks(yo)
            .gpu_threads(yt)
            .unroll(yi);
    }
    row_max.bound(y, 0, M);
    row_max.update().unroll(r);

    prod.compute_at(row_max, yt)
        .tile(x, y, rxi, ryi, tile, tile)
        .unroll(x)
        .unroll(y)
        .tile_init(rxi, ryi);

    prod.update()
        .tile(x, y, rxi, ryi, tile, tile)
        .split(k, rro, rri, tile)
        .reorder(x, y, rro)
        .unroll(x)
        .unroll(y)
        .tile_matmul(rri, rxi, ryi);

    Buffer<float> result = row_max.realize({M}, target);

    for (int y = 0; y < M; y++) {
        float correct = 0;
        for (int x = 0; x < N; x++) {
            float dot = 0;
            for (int k = 0; k < K; k++) {
                dot += (float)A(k, y) * (float)B(x, k);
            }
            correct = (x == 0) ? dot : std::max(correct, dot);
        }
        if (result(y) != correct) {
            printf("result(%d) = %f instead of %f\n", y, result(y), correct);
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
