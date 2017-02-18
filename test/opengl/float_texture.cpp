#include "Halide.h"
#include <stdio.h>

#include "testing.h"

using namespace Halide;

int main() {
    // This test must be run with an OpenGL target.
    const Target target = get_jit_target_from_environment().with_feature(Target::OpenGL);

    Buffer<float> input(255, 255, 3);
    input.fill([](int x, int y, int c) {
        // Note: the following values can be >1.0f to test whether
        // OpenGL performs clamping operations as part of the copy
        // operation.  (It may do so if something other than floats
        // are stored in the actual texture.)
        return (10 * x + y + c);
    });

    Var x, y, c;
    Func g;
    g(x, y, c) = input(x, y, c);

    Buffer<float> out(255, 255, 3);
    g.bound(c, 0, 3);
    g.glsl(x, y, c);
    g.realize(out, target);
    out.copy_to_host();

    if (!Testing::check_result<float>(out, [&](int x, int y, int c) { return input(x, y, c); })) {
        return 1;
    }

    printf("Success!\n");
    return 0;
}
