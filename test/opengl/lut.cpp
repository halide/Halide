#include <Halide.h>
#include <stdio.h>
#include <stdlib.h>

using namespace Halide;

// This test creates two input images and uses one to perform a dependent lookup
// into the other.

int test_lut1d() {

    Var x("x");
    Var y("y");
    Var c("c");

    Image<uint8_t> input(8, 8, 3, "input");
    for (int y=0; y<input.height(); y++) {
        for (int x=0; x<input.width(); x++) {
            float v = (1.0f/16.0f) + (float)x/8.0f;
            input(x, y, 0) = (uint8_t)(v * 255.0f);
            input(x, y, 1) = (uint8_t)((1.0f - v)*255.0f);
            input(x, y, 2) = (uint8_t)((v > 0.5 ? 1.0 : 0.0)*255.0f);
        }
    }

    // 1D Look Up Table case
    Image<float> lut1d(8, 1, 3, "lut1d");
    for (int c = 0; c != 3; ++c) {
        for (int i = 0; i != 8; ++i) {
            lut1d(i, 0, c) = (float)(1 + i);
        }
    }

    Func f0("f");
    Expr e = cast<int>(8.0f * cast<float>(input(x, y, c))/255.0f);

    f0(x, y, c) = lut1d(clamp(e, 0, 7), 0, c);

    Image<float> out0(8, 8, 3,"out");

    f0.bound(c, 0, 3);
    f0.glsl(x, y, c);
    f0.realize(out0);
    out0.copy_to_host();

#if 0
    printf("Input:\n");
    for (int c = 0; c != input.extent(2); ++c) {
        printf("c == %d\n",c);
        for (int y = 0; y != input.extent(1); ++y) {
            for (int x = 0; x != input.extent(0); ++x) {
                printf("%d ", (int)input(x, y, c));
            }
            printf("\n");
        }
    }
    printf("\n");

    printf("LUT:\n");
    for (int c = 0; c != lut1d.extent(2); ++c) {
        printf("c == %d\n",c);
        for (int y=0; y != lut1d.extent(1); ++y) {
            for (int x = 0; x != lut1d.extent(0); ++x) {
                printf("%1.1f ", lut1d(x, y, c));
            }
            printf("\n");
        }
    }
    printf("\n");

    printf("Output:\n");
    for (int c = 0; c != out0.extent(2); ++c) {
        printf("c == %d\n",c);
        for (int y = 0; y != out0.extent(1); ++y) {
            for (int x = 0; x != out0.extent(0); ++x) {
                printf("%1.1f ", out0(x, y, c));
            }
            printf("\n");
        }
    }
#endif

    for (int c = 0; c != out0.extent(2); ++c) {
        for (int y = 0; y != out0.extent(1); ++y) {
            for (int x = 0; x != out0.extent(0); ++x) {
                float expected;
                switch (c) {
                    case 0:
                        expected = (float)(1 + x);
                        break;
                    case 1:
                        expected = (float)(8 - x);
                        break;
                    case 2:
                        expected = x > 3 ? 8.0f : 1.0f;
                        break;
                }
                float result = out0(x, y, c);

                if (result != expected) {
                    fprintf(stderr, "Error at %d,%d,%d %f != %f\n", x, y, c, result, expected);
                    return 1;
                }
            }
        }
    }

    return 0;
}

int main() {

    // This test must be run with an OpenGL target
    const Target &target = get_jit_target_from_environment();
    if (!target.has_feature(Target::OpenGL))  {
        fprintf(stderr,"ERROR: This test must be run with an OpenGL target, e.g. by setting HL_JIT_TARGET=host-opengl.\n");
        return 1;
    }

    if (!test_lut1d()) {
        fprintf(stderr,"Success!\n");
    }



    return 0;
}
