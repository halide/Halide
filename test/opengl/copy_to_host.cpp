#include "Halide.h"
#include <stdio.h>

#include "testing.h"

using namespace Halide;

int main() {
    // This test must be run with an OpenGL target.
    const Target target = get_jit_target_from_environment().with_feature(Target::OpenGL);

    Func gpu("gpu"), cpu("cpu");
    Var x, y, c;

    // Fill buffer using GLSL
    gpu(x, y, c) = cast<uint8_t>(select(c == 0, 10 * x + y,
                                        c == 1, 127,
                                        12));
    gpu.bound(c, 0, 3);
    gpu.glsl(x, y, c);
    gpu.compute_root();

    // This should trigger a copy_to_host operation
    cpu(x, y, c) = gpu(x, y, c);

    Buffer<uint8_t> out(10, 10, 3);
    cpu.realize(out, target);

    if (!Testing::check_result<uint8_t>(out, [&](int x, int y, int c) {
            switch (c) {
                case 0: return 10*x+y;
                case 1: return 127;
                case 2: return 12;
                default: return -1;
            } })) {
        return 1;
    }

    printf("Success!\n");
    return 0;
}
