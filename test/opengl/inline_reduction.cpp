#include "Halide.h"

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

    for (int c = 0; c < result.channels(); c++) {
        for (int y = 0; y < result.height(); y++) {
            for (int x = 0; x < result.width(); x++) {
                float correct = 45;
                if (result(x, y, c) != correct) {
                    printf("result(%d, %d, %d) = %f instead of %f\n",
                           x, y, c, result(x, y, c), correct);
                    return -1;
                }
            }
        }
    }

    printf("Success!\n");

    return 0;
}
