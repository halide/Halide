#include "Halide.h"
#include <stdio.h>

#include "testing.h"

using namespace Halide;

int main() {
    // This test must be run with an OpenGL target.
    const Target target = get_jit_target_from_environment().with_feature(Target::OpenGL);

    Func f;
    Var x, y, c;
    RDom r(0, 10);
    f(x, y, c) = sum(cast<float>(r));
    f.bound(c, 0, 3).glsl(x, y, c);

    Buffer<float> result = f.realize(100, 100, 3, target);

    if (!Testing::check_result<float>(result, [&](int x, int y, int c) { return 45; })) {
        return 1;
    }

    printf("Success!\n");

    return 0;
}
