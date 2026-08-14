// A matrix multiply whose result is then reduced along its rows. The reduction
// crosses the lanes a tensor core tile is spread over, so today the tile has to
// be written out to shared memory and read back to do it. This test pins down
// what the answer should be, so that a version that reduces the fragments in
// place has something to be checked against.

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

    const int M = 64, N = 64, K = 64;
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
    Func prod("prod"), staged("staged"), row_max("row_max");

    prod(x, y) = 0.f;
    prod(x, y) += cast<float>(A(k, y)) * cast<float>(B(x, k));

    // The tile is stored out to shared memory, which is what makes the whole
    // row reachable from one thread.
    staged(x, y) = prod(x, y);

    RDom r(0, N, "r");
    row_max(y) = maximum(staged(r, y));

    Var yi("yi"), mmxi("mmxi"), mmyi("mmyi"), rxi("rxi"), ryi("ryi");
    RVar rro("rro"), rri("rri");

    // A block owns a strip of `tile` rows and the whole width, so that a row's
    // maximum is entirely inside it.
    row_max.bound(y, 0, M)
        .split(y, y, yi, tile)
        .gpu_blocks(y)
        .gpu_threads(yi);

    staged.compute_at(row_max, y)
        .store_in(MemoryType::GPUShared)
        .tile(x, y, mmxi, mmyi, tile, tile)
        .unroll(x)
        .tile_store(mmxi, mmyi);

    prod.compute_at(staged, x)
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
