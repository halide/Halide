#include "Halide.h"

using namespace Halide;

int main() {

    // This test must be run with an OpenGL target
    const Target &target = get_jit_target_from_environment();
    if (!target.has_feature(Target::OpenGL)) {
        fprintf(stderr, "ERROR: This test must be run with an OpenGL target, e.g. by setting HL_JIT_TARGET=host-opengl.\n");
        return 1;
    }

    Func f,g;
    Var x, y, c;
    g(x, y, c) = cast<uint8_t>(x);
    f(x, y, c) = g(x, y, c) + cast<uint8_t>(y);
    f.bound(c, 0, 3).glsl(x, y, c);
    g.bound(c, 0, 3).compute_root().glsl(x, y, c);

    Image<uint8_t> result = f.realize(100, 100, 3);

    printf("Success!\n");

    return 0;
}
