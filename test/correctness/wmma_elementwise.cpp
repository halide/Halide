// An elementwise op applied to a tensor core tile where it sits, rather than
// on the way out to memory. Every lane holds the same entries before and
// after, so the op runs on the registers the tile is spread over and never has
// to know which entries those are.

#include "Halide.h"
#include <cstdio>
using namespace Halide;
int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (!target.has_feature(Target::CUDA)) { printf("[SKIP]\n"); return 0; }
    if (target.get_cuda_capability_lower_bound() < 70) { printf("[SKIP]\n"); return 0; }
    const int tile = 16;
    Buffer<float16_t> A(tile, tile), B(tile, tile);
    for (int y = 0; y < tile; y++)
        for (int x = 0; x < tile; x++) {
            A(x, y) = float16_t((float)(rand() & 3));
            B(x, y) = float16_t((float)(rand() & 3));
        }

    Var x("x"), y("y");
    RDom k(0, tile, "k");
    Func prod("prod"), soft("soft"), out("out");
    prod(x, y) = 0.f;
    prod(x, y) += cast<float>(A(k, y)) * cast<float>(B(x, k));
    // An elementwise op on the tile, where the tile sits.
    soft(x, y) = max(prod(x, y) * 2.f - 1.f, 0.f);
    out(x, y) = soft(x, y);

    Var xo("xo"), xi("xi"), yo("yo"), yi("yi"), rxi("rxi"), ryi("ryi");
    RVar rro("rro"), rri("rri");
    out.bound(x, 0, tile).bound(y, 0, tile)
        .split(y, yo, yi, tile).split(x, xo, xi, tile)
        .reorder(xi, yi, xo, yo).gpu_blocks(yo).gpu_threads(xo)
        .unroll(xi).unroll(yi).tile_store(xi, yi);
    soft.compute_at(out, xo).store_in(MemoryType::Tile)
        .tile(x, y, rxi, ryi, tile, tile).unroll(x).unroll(y).tile_init(rxi, ryi);
    prod.compute_at(out, xo).tile(x, y, rxi, ryi, tile, tile)
        .unroll(x).unroll(y).tile_init(rxi, ryi);
    prod.update().tile(x, y, rxi, ryi, tile, tile).split(k, rro, rri, tile)
        .reorder(x, y, rro).unroll(x).unroll(y).tile_matmul(rri, rxi, ryi);

    Buffer<float> result = out.realize({tile, tile}, target);
    for (int y = 0; y < tile; y++) {
        for (int x = 0; x < tile; x++) {
            float dot = 0;
            for (int k = 0; k < tile; k++) dot += (float)A(k, y) * (float)B(x, k);
            float correct = std::max(dot * 2.f - 1.f, 0.f);
            if (result(x, y) != correct) {
                printf("result(%d, %d) = %f instead of %f\n", x, y, result(x, y), correct);
                return 1;
            }
        }
    }
    printf("Success!\n");
    return 0;
}
