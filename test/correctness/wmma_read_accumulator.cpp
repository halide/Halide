// Read every entry of a tensor core accumulator back out of the registers it
// is spread across, instead of copying the tile out to memory. Getting all 256
// of them right means the runtime measured the layout of a fragment and
// patched every shuffle to match.

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

    const int M = 16, N = 32, K = 16;
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
    Func prod("prod"), out("out");

    prod(x, y) = 0.f;
    prod(x, y) += cast<float>(A(k, y)) * cast<float>(B(x, k));

    out(x, y) = prod(x, y);

    Var xo("xo"), xi("xi"), yo("yo"), yi("yi"), rxi("rxi"), ryi("ryi");
    RVar rro("rro"), rri("rri");

    // One warp per tile, which reads its whole tile back out of its registers.
    out.bound(x, 0, N)
        .bound(y, 0, M)
        .split(y, yo, yi, tile)
        .split(x, xo, xi, tile)
        .reorder(xi, yi, xo, yo)
        .gpu_blocks(yo)
        .gpu_threads(xo)
        .unroll(xi)
        .unroll(yi);

    prod.compute_at(out, xo)
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

    Buffer<float> result = out.realize({N, M}, target);

    for (int y = 0; y < M; y++) {
        for (int x = 0; x < N; x++) {
            float correct = 0;
            for (int k = 0; k < K; k++) {
                correct += (float)A(k, y) * (float)B(x, k);
            }
            if (result(x, y) != correct) {
                printf("result(%d, %d) = %f instead of %f\n", x, y, result(x, y), correct);
                return 1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
