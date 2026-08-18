// A whole softmax over the rows of a matrix multiply, without the tile ever
// leaving the registers it was computed in: the row maximum, the exponential,
// the row sum, and the division all happen where the fragments sit. The
// exponential is what makes this need calls, lets and reinterprets to be
// restricted to a lane's share of the tile, which fast_exp expands into. The
// two reductions are what make it need the lanes of the warp to exchange
// entries along a row.

#include "Halide.h"
#include <cstdio>

using namespace Halide;

int run(bool fast) {
    Target target = get_jit_target_from_environment();

    const int M = 32, N = 32, K = 32;
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
    Func prod("prod"), m("m"), e("e"), sum_e("sum_e"), soft("soft"), out("out");

    prod(x, y) = 0.f;
    prod(x, y) += cast<float>(A(k, y)) * cast<float>(B(x, k));

    // The row statistics are one value per row, but they are stored as whole
    // tiles with that value repeated along the row. That is what a reduction
    // along an axis leaves behind anyway, and it makes reading them back
    // alongside the tile they came from cost nothing.
    m(y) = -1e30f;
    m(y) = max(m(y), prod(r, y));

    e(x, y) = fast ? fast_exp(prod(x, y) - m(y)) : exp(prod(x, y) - m(y));

    sum_e(y) = 0.f;
    sum_e(y) += e(r, y);

    soft(x, y) = e(x, y) / sum_e(y);
    out(x, y) = soft(x, y);

    Var xo("xo"), yo("yo"), xio("xio"), yio("yio"), xt("xt"), xi("xi"), yi("yi");
    Var rxi("rxi"), ryi("ryi");
    RVar rro("rro"), rri("rri"), ry("ry");

    // One warp owns the whole matrix. A softmax reduces along the rows, so a
    // block has to hold whole ones.
    out.bound(x, 0, N)
        .bound(y, 0, M)
        .tile(x, y, xo, yo, xi, yi, N, M)
        .tile(xi, yi, xio, yio, xi, yi, tile, tile)
        .gpu_blocks(xo, yo)
        .unroll(xio)
        .unroll(yio)
        .tile_store(xi, yi);

    soft.compute_at(out, xo)
        .store_in(MemoryType::Tile)
        .tile(x, y, rxi, ryi, tile, tile)
        .unroll(x)
        .unroll(y)
        .tile_init(rxi, ryi);

    m.store_in(MemoryType::Tile)
        .compute_at(out, xo)
        .split(y, y, ryi, tile)
        .unroll(y)
        .vectorize(ryi);

    m.update()
        .split(y, y, ryi, tile)
        .split(r, rro, rri, tile)
        .unroll(y)
        .unroll(rro)
        .tile_reduce(rri, ryi);

    e.compute_at(out, xo)
        .store_in(MemoryType::Tile)
        .tile(x, y, rxi, ryi, tile, tile)
        .unroll(x)
        .unroll(y)
        .tile_init(rxi, ryi);

    sum_e.store_in(MemoryType::Tile)
        .compute_at(out, xo)
        .split(y, y, ryi, tile)
        .unroll(y)
        .vectorize(ryi);

    sum_e.update()
        .split(y, y, ryi, tile)
        .split(r, rro, rri, tile)
        .unroll(y)
        .unroll(rro)
        .tile_reduce(rri, ryi);

    prod.compute_at(out, xo)
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
        float total = 0;
        for (int x = 0; x < N; x++) {
            total += std::exp(row[x] - row_max);
        }
        for (int x = 0; x < N; x++) {
            float correct = std::exp(row[x] - row_max) / total;
            if (std::abs(result(x, y) - correct) > (fast ? 1e-4f : 1e-5f)) {
                printf("%s: result(%d, %d) = %f instead of %f\n",
                       fast ? "fast_exp" : "exp", x, y, result(x, y), correct);
                return 1;
            }
        }
    }

    return 0;
}

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

    for (bool fast : {false, true}) {
        if (run(fast)) {
            return 1;
        }
    }
    printf("Success!\n");
    return 0;
}
