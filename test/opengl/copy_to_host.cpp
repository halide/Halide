#include "Halide.h"
#include <stdio.h>
#include <stdlib.h>

using namespace Halide;

int main() {

    // This test must be run with an OpenGL target
    const Target &target = get_jit_target_from_environment();
    if (!target.has_feature(Target::OpenGL))  {
        fprintf(stderr,"ERROR: This test must be run with an OpenGL target, e.g. by setting HL_JIT_TARGET=host-opengl.\n");
        return 1;
    }

    Func gpu("gpu"), cpu("cpu");
    Var x, y, c;

    // Fill buffer using GLSL
    gpu(x, y, c) = cast<uint8_t>(select(c == 0, 10*x + y,
                                        c == 1, 127,
                                        12));
    gpu.bound(c, 0, 3);
    gpu.glsl(x, y, c);
    gpu.compute_root();

    // This should trigger a copy_to_host operation
    cpu(x, y, c) = gpu(x, y, c);

    Image<uint8_t> out(10, 10, 3);
    cpu.realize(out);

    for (int y=0; y<out.height(); y++) {
        for (int x=0; x<out.width(); x++) {
            if (!(out(x, y, 0) == 10*x+y && out(x, y, 1) == 127 && out(x, y, 2) == 12)) {
                fprintf(stderr, "Incorrect pixel (%d, %d, %d) at x=%d y=%d.\n",
                        out(x, y, 0), out(x, y, 1), out(x, y, 2),
                        x, y);
                return 1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
