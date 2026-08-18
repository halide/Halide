// Vectorized casts from float to half on the GPU.
//
// LLVM's NVPTX backend got this wrong until version 22: it dropped the
// conversion entirely and stored the wider values' bit patterns, so a float of
// 7.0f came back as the two halves 0x40E00000 splits into, with nothing
// diagnosed. That is behind us, but the test is cheap and it covers a
// conversion every backend has to get right.

#include "Halide.h"
#include <cstdio>

using namespace Halide;

namespace {

// Cast a float buffer to T, vectorized `vec` lanes at a time. The output has
// to be a buffer of T rather than a round trip back to float, because what
// goes wrong is the conversion feeding a store of the narrow type.
template<typename T>
int test(const char *name, int vec, bool lanes) {
    const int W = 32 * vec, rows = 8, H = rows * 16;

    Buffer<float> in(W, H);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            // Values a 16-bit float holds exactly, so the round trip is exact
            // and the check can be for equality.
            in(x, y) = (float)((x * 7 + y * 13) % 23);
        }
    }
    in.set_host_dirty();

    Var x("x"), y("y"), xi("xi"), xv("xv"), yo("yo"), yi("yi");
    Func out("out");

    out(x, y) = cast<T>(in(x, y));

    out.bound(x, 0, W)
        .bound(y, 0, H)
        .split(x, xi, xv, vec)
        .split(y, yo, yi, rows)
        .reorder(xv, xi, yi, yo)
        .gpu_blocks(yo)
        .vectorize(xv);
    if (lanes) {
        out.gpu_threads(yi).gpu_lanes(xi);
    } else {
        out.gpu_threads(xi, yi);
    }

    Buffer<T> result(W, H);
    out.realize(result, get_jit_target_from_environment());
    result.copy_to_host();

    int bad = 0;
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            if ((float)result(x, y) != in(x, y)) {
                if (bad++ < 3) {
                    printf("%s vec=%d %s: result(%d, %d) = %f instead of %f\n",
                           name, vec, lanes ? "gpu_lanes" : "gpu_threads",
                           x, y, (float)result(x, y), in(x, y));
                }
            }
        }
    }
    return bad != 0;
}

}  // namespace

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (!target.has_feature(Target::CUDA)) {
        printf("[SKIP] This test only covers the CUDA backend.\n");
        return 0;
    }

    int failures = 0;
    for (int vec : {2, 3, 4, 8}) {
        for (bool lanes : {false, true}) {
            failures += test<float16_t>("float16", vec, lanes);
        }
    }

    if (failures) {
        printf("%d configuration(s) failed\n", failures);
        return 1;
    }
    printf("Success!\n");
    return 0;
}
