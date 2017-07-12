#include "Halide.h"
#include <stdio.h>

#include "testing.h"

using namespace Halide;

int main() {
    // This test must be run with an OpenGL target.
    const Target target = get_jit_target_from_environment().with_feature(Target::OpenGL);

    Buffer<uint8_t> input(255, 10, 3);
    input.fill([](int x, int y, int c) {
        return 10 * x + y + c;
    });

    Var x, y, c;
    Func g;
    g(x, y, c) = { input(x, y, c), input(x, y, c) / 2 };

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

    if (!Testing::check_result<uint8_t>(out, [&](int x, int y, int c) { return input(x, y, c) / 2; })) {
        return 1;
    }

    printf("Success!\n");
    return 0;
}
