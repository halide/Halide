#include "Halide.h"
#include <stdio.h>
#include <stdlib.h>

using namespace Halide;

int main() {
    // This test must be run with an OpenGL target.
    const Target target = get_jit_target_from_environment().with_feature(Target::OpenGL);

    Buffer<uint8_t> input(255, 10, 3);
    for (int y = 0; y < input.height(); y++) {
        for (int x = 0; x < input.width(); x++) {
            for (int c = 0; c < 3; c++) {
                input(x, y, c) = 10*x + y + c;
            }
        }
    }

    Var x, y, c;
    Func g;
    g(x, y, c) = {input(x, y, c), input(x, y, c) / 2};

    // h will be an opengl stage with tuple input. Tuple outputs
    // aren't supported because OpenGL ES 2.0 doesn't support multiple
    // output textures.
    Func h;
    h(x, y, c) = min(g(x, y, c)[0], g(x, y, c)[1]);

    Buffer<uint8_t> out(255, 10, 3);
    g.compute_root();
    h.compute_root().bound(c, 0, 3).glsl(x, y, c);

    h.realize(out, target);
    out.copy_to_host();

    for (int y = 0; y < out.height(); y++) {
        for (int x = 0; x < out.width(); x++) {
            if (!(out(x, y, 0) == input(x, y, 0) / 2 &&
                  out(x, y, 1) == input(x, y, 1) / 2 &&
                  out(x, y, 2) == input(x, y, 2) / 2)) {
                fprintf(stderr, "Incorrect pixel (%d, %d, %d) != (%d, %d, %d) at x=%d y=%d.\n",
                        out(x, y, 0), out(x, y, 1), out(x, y, 2),
                        input(x, y, 0) / 2, input(x, y, 1) / 2, input(x, y, 2) / 2,
                        x, y);
                return 1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
