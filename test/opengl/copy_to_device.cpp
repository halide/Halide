#include "Halide.h"
#include <stdio.h>
#include <stdlib.h>

using namespace Halide;

// Test that internal allocations work correctly with copy_to_device.
// This requires that suitable buffer_t objects are created internally.
int main() {

    // This test must be run with an OpenGL target
    const Target &target = get_jit_target_from_environment();
    if (!target.has_feature(Target::OpenGL))  {
        fprintf(stderr,"ERROR: This test must be run with an OpenGL target, e.g. by setting HL_JIT_TARGET=host-opengl.\n");
        return 1;
    }

    Image<uint8_t> input(255, 10, 3);
    for (int y=0; y<input.height(); y++) {
        for (int x=0; x<input.width(); x++) {
            for (int c=0; c<3; c++) {
              input(x, y, c) = 10*x + y + c;
            }
        }
    }

    Var x, y, c;
    Func g, h;
    h(x, y, c) = input(x, y, c);
    h.compute_root();  // force internal allocation of h

    // access h from shader to trigger copy_to_device operation
    g(x, y, c) = h(x, y, c);
    g.bound(c, 0, 3);
    g.glsl(x, y, c);

    Image<uint8_t> out(255, 10, 3);
    g.realize(out);
    out.copy_to_host();

    for (int y=0; y<out.height(); y++) {
        for (int x=0; x<out.width(); x++) {
            if (!(out(x, y, 0) == input(x, y, 0) &&
                  out(x, y, 1) == input(x, y, 1) &&
                  out(x, y, 2) == input(x, y, 2))) {
                fprintf(stderr, "Incorrect pixel (%d,%d,%d) != (%d,%d,%d) at x=%d y=%d.\n",
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
