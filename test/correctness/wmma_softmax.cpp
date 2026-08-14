// Subtract the maximum of each row of a matrix multiply from it without the
// tile ever leaving the registers it was computed in. The subtrahend covers the
// tile, but as a vector spread along one axis rather than as a fragment, so
// which entry of it a lane wants varies from lane to lane.

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
    Func prod("prod"), m("m"), soft("soft"), out("out");

    prod(x, y) = 0.f;
    prod(x, y) += cast<float>(A(k, y)) * cast<float>(B(x, k));

    // Nothing here says how m is spread over the lanes of a warp, and it isn't
    // spread the way prod is: every lane ends up holding the whole of it.
    m(y) = -1e30f;
    m(y) = max(m(y), prod(r, y));

    soft(x, y) = prod(x, y) - m(y);
    out(x, y) = soft(x, y);

    Var xo("xo"), yo("yo"), xt("xt"), xi("xi"), yi("yi");
    Var rxi("rxi"), ryi("ryi");
    RVar rro("rro"), rri("rri");

    // One warp owns the whole tile.
    out.bound(x, 0, N)
        .bound(y, 0, M)
        .tile(x, y, xo, yo, xi, yi, tile, tile)
        .split(xo, xo, xt, 1)
        .reorder(xi, yi, xt, xo, yo)
        .gpu_blocks(xo, yo)
        .gpu_threads(xt)
        .unroll(xi)
        .unroll(yi);

    soft.compute_at(out, xt)
        .store_in(MemoryType::Tile)
        .tile(x, y, rxi, ryi, tile, tile)
        .unroll(x)
        .unroll(y)
        .tile_init(rxi, ryi);

    m.compute_at(out, xt).unroll(y);
    m.update().unroll(r).unroll(y);

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
        float row[N], row_max = -1e30f;
        for (int x = 0; x < N; x++) {
            row[x] = 0;
            for (int k = 0; k < K; k++) {
                row[x] += (float)A(k, y) * (float)B(x, k);
            }
            row_max = std::max(row_max, row[x]);
        }
        for (int x = 0; x < N; x++) {
            float correct = row[x] - row_max;
            if (result(x, y) != correct) {
                printf("result(%d, %d) = %f instead of %f\n",
                       x, y, result(x, y), correct);
                return 1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
