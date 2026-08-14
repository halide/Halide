// Feed the accumulator of one matrix multiply straight into the next as its a
// operand. The two hold a matrix differently, but both layouts are register
// layouts, so the handoff is a convert and a pack with no cross-lane traffic
// and no trip through shared memory.

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

    const int M = 16, N = 16, K = 16, tile = 16;

    Buffer<float16_t> A(K, M), B(N, K), C(N, K);
    for (int y = 0; y < M; y++)
        for (int x = 0; x < K; x++) A(x, y) = float16_t((float)(rand() & 3));
    for (int y = 0; y < K; y++)
        for (int x = 0; x < N; x++) {
            B(x, y) = float16_t((float)(rand() & 3));
            C(x, y) = float16_t((float)(rand() & 3));
        }

    Var x("x"), y("y");
    RDom k(0, K, "k"), k2(0, K, "k2");
    Func prod("prod"), chain("chain"), out("out");

    prod(x, y) = 0.f;
    prod(x, y) += cast<float>(A(k, y)) * cast<float>(B(x, k));

    chain(x, y) = 0.f;
    chain(x, y) += cast<float>(prod(k2, y)) * cast<float>(C(x, k2));

    out(x, y) = chain(x, y);

    Var xo("xo"), yo("yo"), xt("xt"), xi("xi"), yi("yi");
    Var rxi("rxi"), ryi("ryi");
    RVar rro("rro"), rri("rri");

    out.bound(x, 0, N).bound(y, 0, M)
        .tile(x, y, xo, yo, xi, yi, tile, tile)
        .split(xo, xo, xt, 1)
        .reorder(xi, yi, xt, xo, yo)
        .gpu_blocks(xo, yo)
        .gpu_threads(xt)
        .unroll(xi)
        .unroll(yi);
    chain.compute_at(out, xt)
        .store_in(MemoryType::Tile)
        .tile(x, y, rxi, ryi, tile, tile)
        .unroll(x)
        .unroll(y)
        .tile_init(rxi, ryi);
    chain.update()
        .tile(x, y, rxi, ryi, tile, tile)
        .split(k2, rro, rri, tile)
        .reorder(x, y, rro)
        .unroll(x)
        .unroll(y)
        .tile_matmul(rri, rxi, ryi);

    prod.compute_at(out, xt)
        .store_in(MemoryType::Tile)
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
            for (int i = 0; i < K; i++) {
                float p = 0;
                for (int j = 0; j < K; j++) p += (float)A(j, y) * (float)B(i, j);
                correct += (float)float16_t(p) * (float)C(x, i);
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
