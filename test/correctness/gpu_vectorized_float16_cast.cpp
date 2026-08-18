// Vectorized casts from float to half on the GPU.
//
// Narrowing a float to a 16-bit one two lanes at a time is miscompiled by
// LLVM's NVPTX backend as of 21.1: the conversion is dropped entirely and the
// wider values' bits are stored instead, so a float of 7.0f comes back as the
// two halves its bit pattern splits into. Wider vectors are cut into pairs on
// the way down, so they were wrong too, and nothing about it is diagnosed -
// the answer is just quietly wrong. CodeGen_PTX_Dev works around it by
// converting a lane at a time.
//
// Bfloats are not covered: a vectorized cast to one asserts inside LLVM
// before it gets anywhere near this, which is a separate bug.

#include "Halide.h"
#include <cstdio>

using namespace Halide;

namespace {

// Cast a float buffer to T and back, vectorized `vec` lanes at a time.
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
    Func narrow("narrow"), out("out");

    narrow(x, y) = cast<T>(in(x, y));
    out(x, y) = cast<float>(narrow(x, y));

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
    narrow.compute_at(out, xi).vectorize(x);

    Buffer<float> result(W, H);
    out.realize(result, get_jit_target_from_environment());
    result.copy_to_host();

    int bad = 0;
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            if (result(x, y) != in(x, y)) {
                if (bad++ < 3) {
                    printf("%s vec=%d %s: result(%d, %d) = %f instead of %f\n",
                           name, vec, lanes ? "gpu_lanes" : "gpu_threads",
                           x, y, result(x, y), in(x, y));
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
