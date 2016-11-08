#include "Halide.h"
#include <stdio.h>
#include <stdlib.h>

using namespace Halide;

int main() {
    // This test must be run with an OpenGL target.
    const Target target = get_jit_target_from_environment().with_feature(Target::OpenGL);

    Func f;
    Var x, y, c;

    f(x, y, c) = cast<uint8_t>(42);

    Buffer<uint8_t> out(10, 10, 3);
    f.bound(c, 0, 3).glsl(x, y, c);
    f.realize(out, target);

    out.copy_to_host();
    for (int y=0; y<out.height(); y++) {
        for (int x=0; x<out.width(); x++) {
            for (int z=0; x<out.channels(); x++) {
                if (!(out(x, y, z) == 42)) {
                    fprintf(stderr, "Incorrect pixel (%d) at x=%d y=%d z=%d.\n",
                            out(x, y, z), x, y, z);
                    return 1;
                }
            }
        }
    }

    printf("Success!\n");
    return 0;
}
