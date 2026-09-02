// A Tuple-valued tensor core accumulator, advanced by one update over a
// reduction. Each component's new value reads the other component as it
// was, so every value is computed before any is written, which lowering
// does through lets ahead of the stores; each such value gets a tile of
// its own.

#include "Halide.h"
#include <cstdio>
using namespace Halide;
int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (!target.has_feature(Target::CUDA)) { printf("[SKIP]\n"); return 0; }
    if (target.get_cuda_capability_lower_bound() < 70) { printf("[SKIP]\n"); return 0; }
    const int tile = 16, steps = 3;
    Buffer<float16_t> A(tile, tile), B(tile, tile);
    for (int y = 0; y < tile; y++)
        for (int x = 0; x < tile; x++) {
            A(x, y) = float16_t((float)(rand() & 3));
            B(x, y) = float16_t((float)(rand() & 3));
        }

    Var x("x"), y("y");
    RDom k(0, tile, "k"), r(0, steps, "r");
    Func prod("prod"), state("state"), diff("diff"), out("out");
    prod(x, y) = 0.f;
    prod(x, y) += cast<float>(A(k, y)) * cast<float>(B(x, k));
    // The components swap places each step, one picking up the product and
    // the step number.
    state(x, y) = {0.f, 1.f};
    state(x, y) = {state(x, y)[1] + prod(x, y) + cast<float>(r), state(x, y)[0] * 2.f};
    diff(x, y) = state(x, y)[0] - state(x, y)[1];
    out(x, y) = diff(x, y);

    Var xo("xo"), xi("xi"), yo("yo"), yi("yi"), rxi("rxi"), ryi("ryi");
    RVar rro("rro"), rri("rri");
    out.bound(x, 0, tile).bound(y, 0, tile)
        .split(y, yo, yi, tile).split(x, xo, xi, tile)
        .reorder(xi, yi, xo, yo).gpu_blocks(yo).gpu_threads(xo)
        .unroll(xi).unroll(yi).tile_store(xi, yi);
    diff.compute_at(out, xo).store_in(MemoryType::Tile)
        .tile(x, y, rxi, ryi, tile, tile).unroll(x).unroll(y).tile_init(rxi, ryi);
    state.compute_at(out, xo).store_in(MemoryType::Tile)
        .tile(x, y, rxi, ryi, tile, tile).unroll(x).unroll(y).tile_init(rxi, ryi);
    state.update().tile(x, y, rxi, ryi, tile, tile).unroll(x).unroll(y).tile_init(rxi, ryi);
    prod.compute_at(out, xo).store_in(MemoryType::Tile).tile(x, y, rxi, ryi, tile, tile)
        .unroll(x).unroll(y).tile_init(rxi, ryi);
    prod.update().tile(x, y, rxi, ryi, tile, tile).split(k, rro, rri, tile)
        .reorder(x, y, rro).unroll(x).unroll(y).tile_matmul(rri, rxi, ryi);

    Buffer<float> result = out.realize({tile, tile}, target);
    for (int y = 0; y < tile; y++) {
        for (int x = 0; x < tile; x++) {
            float dot = 0;
            for (int k = 0; k < tile; k++) dot += (float)A(k, y) * (float)B(x, k);
            float s0 = 0.f, s1 = 1.f;
            for (int i = 0; i < steps; i++) {
                float n0 = s1 + dot + i, n1 = s0 * 2.f;
                s0 = n0;
                s1 = n1;
            }
            float correct = s0 - s1;
            if (result(x, y) != correct) {
                printf("result(%d, %d) = %f instead of %f\n", x, y, result(x, y), correct);
                return 1;
            }
        }
    }
    printf("Success!\n");
    return 0;
}
