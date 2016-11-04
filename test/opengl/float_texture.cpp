#include "Halide.h"
#include <stdio.h>
#include <stdlib.h>

using namespace Halide;

int main() {
    // This test must be run with an OpenGL target.
    const Target target = get_jit_target_from_environment().with_feature(Target::OpenGL);

    Buffer<float> input(255, 255, 3);
    for (int y=0; y<input.height(); y++) {
        for (int x=0; x<input.width(); x++) {
            for (int c=0; c<3; c++) {
                // Note: the following values can be >1.0f to test whether
                // OpenGL performs clamping operations as part of the copy
                // operation.  (It may do so if something other than floats
                // are stored in the actual texture.)
                float v = (10 * x + y + c);
                input(x, y, c) = v;
            }
        }
    }

    Var x, y, c;
    Func g;
    g(x, y, c) = input(x, y, c);

    Buffer<float> out(255, 255, 3);
    g.bound(c, 0, 3);
    g.glsl(x, y, c);
    g.realize(out, target);
    out.copy_to_host();

    for (int y=0; y<out.height(); y++) {
        for (int x=0; x<out.width(); x++) {
            if (!(out(x, y, 0) == input(x, y, 0) &&
                  out(x, y, 1) == input(x, y, 1) &&
                  out(x, y, 2) == input(x, y, 2))) {
                fprintf(stderr, "Incorrect pixel (%g,%g,%g) != (%g,%g,%g) at x=%d y=%d.\n",
                        out(x, y, 0), out(x, y, 1), out(x, y, 2),
                        input(x, y, 0), input(x, y, 1), input(x, y, 2),
                        x, y);
                return 1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
