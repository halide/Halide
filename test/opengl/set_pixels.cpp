#include "Halide.h"
#include <stdio.h>

#include "testing.h"

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
    if (!Testing::check_result<uint8_t>(out, [](int x, int y, int c) { return 42; })) {
        return 1;
    }

    printf("Success!\n");
    return 0;
}
