#include <Halide.h>
#include <stdio.h>
#include <stdlib.h>

using namespace Halide;

// This test creates two input images and uses one to perform a dependent lookup
// into the other.

int main() {

    // This test must be run with an OpenGL target
    const Target &target = get_jit_target_from_environment();
    if (!target.has_feature(Target::OpenGL))  {
        fprintf(stderr,"ERROR: This test must be run with an OpenGL target, e.g. by setting HL_JIT_TARGET=host-opengl.\n");
        return 1;
    }

    Var x("x");
    Var y("y");
    Var c("c");

    Image<uint8_t> input(8, 8, 3);
    for (int y=0; y<input.height(); y++) {
        for (int x=0; x<input.width(); x++) {
            for (int c=0; c<3; c++) {
                input(x, y, c) = 10*x + y;
            }
        }
    }

    // 1D Look Up Table case
    Image<float> lut1d(16, 1, 3);
    for (int i=0; i!=16; ++i) {
        for (int c=0; c!=3; ++c) {
            lut1d(i,1,c) = ((float)i) / 16.0f;
        }
    }

    Func f0("f");
    f0(x,y,c) = lut1d(input(x,y,c),1,c);

    Image<float> out0(8, 8, 3);

    f0.bound(c, 0, 3);
    f0.glsl(x, y, c);
    f0.realize(out0);
    out0.copy_to_host();

    for (int c=0; c != out0.extent(2); ++c) {
        for (int y=0; y != out0.extent(1); ++y) {
            for (int x=0; x != out0.extent(0); ++x) {
                printf("%f ",out0(x,y,c));
            }
            printf("\n");
        }
    }

    // 2D Look Up Table case
    Image<float> lut2d(16, 1, 3);
    for (int i=0; i!=16; ++i) {
        for (int j=0; j!=16; ++j) {
            for (int c=0; c!=3; ++c) {
                lut2d(i,j,c) = ((float)i) / 16.0f;
            }
        }
    }

    Func f1("f");
    Expr x0 = input(x,y,0);
    Expr y0 = input(x,y,1);
    f1(x,y,c) = lut2d(x0,y0,c);

    Image<float> out1(8, 8, 3);

    f1.bound(c, 0, 3);
    f1.glsl(x, y, c);
    f1.realize(out1);
    out1.copy_to_host();

    for (int c=0; c != out1.extent(2); ++c) {
        for (int y=0; y != out1.extent(1); ++y) {
            for (int x=0; x != out1.extent(0); ++x) {
                printf("%f ",out1(x,y,c));
            }
            printf("\n");
        }
    }

    return 0;
}
