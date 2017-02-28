#include "Halide.h"
#include <stdio.h>

#include "testing.h"

using namespace Halide;

// This test creates two input images and uses one to perform a dependent lookup
// into the other. The lookup table is produced using a Halide func scheduled
// on the host.

int test_lut1d() {

    // This test must be run with an OpenGL target.
    const Target target = get_jit_target_from_environment().with_feature(Target::OpenGL);

    Var x("x");
    Var y("y");
    Var c("c");

    Buffer<uint8_t> input(8, 8, 3);
    input.fill([](int x, int y, int c) {
            float v = (1.0f / 16.0f) + (float)x / 8.0f;
	    switch (c) {
	    case 0: return (uint8_t)(v * 255.0f);
	    case 1: return (uint8_t)((1.0f - v) * 255.0f);
	    default: return (uint8_t)((v > 0.5 ? 1.0 : 0.0) * 255.0f);
        } });

    // 1D Look Up Table case
    Func lut1d("lut1d");
    lut1d(x) = cast<float>(1 + x);

    Func f0("f");
    Expr e = cast<int>(8.0f * cast<float>(input(x, y, c)) / 255.0f);

    f0(x, y, c) = lut1d(clamp(e, 0, 7));
    lut1d.compute_root();

    f0.bound(c, 0, 3);
    f0.glsl(x, y, c);

    Buffer<float> out0(8, 8, 3);
    f0.realize(out0, target);

    out0.copy_to_host();

    if (!Testing::check_result<float>(out0, [](int x, int y, int c) {
	    switch (c) {
                case 0: return (float)(1 + x);
                case 1: return (float)(8 - x);
                case 2: return (x > 3) ? 8.0f : 1.0f;
		default: return -1.0f;
	    } })) {
        return 1;
    }

    return 0;
}

int main() {

    if (test_lut1d() == 0) {
        printf("PASSED\n");
    }

    return 0;
}
