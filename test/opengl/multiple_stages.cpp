#include "Halide.h"

using namespace Halide;

int main() {

    // This test must be run with an OpenGL target.
    const Target target = get_jit_target_from_environment().with_feature(Target::OpenGL);

    Func f,g,h;
    Var x, y, c;
    g(x, y, c) = cast<uint8_t>(x);
    h(x, y, c) = 1 + g(x, y, c);
    f(x, y, c) = h(x, y, c) + cast<uint8_t>(y);
    f.bound(c, 0, 3).glsl(x, y, c);
    h.bound(c, 0, 3).compute_root();
    g.bound(c, 0, 3).compute_root().glsl(x, y, c);

    Buffer<uint8_t> result = f.realize(10, 10, 3, target);
    result.copy_to_host();

    for (int i=0; i<10; i++) {
        for (int j=0; j<10; j++) {
            for (int k=0; k<3; k++) {
                if (result(i,j,k) != i+j+1) {
                    printf("FAILED: expected %d but got %d\n",
                      i+j+1, result(i,j,k));
                  return 1;
                }
            }
        }
    }

    Func f2, g2;
    f2(x, y, c) = cast<float>(x);
    g2(x, y, c) = f2(x, y, c) + cast<float>(y);

    f2.bound(c, 0, 3).glsl(x, y, c).compute_root();
    g2.bound(c, 0, 3).glsl(x, y, c);

    Buffer<float> result2 = g2.realize(10, 10, 3, target);
    for (int i=0; i<10; i++) {
        for (int j=0; j<10; j++) {
            for (int k=0; k<3; k++) {
                float diff = (result2(i,j,k) - (float)(i+j));
                diff *= diff;
                if (diff > 0.0001) {
                    printf("FAILED: expected %f but got %f (diff is %f)\n",
                      (float)(i+j), result2(i,j,k), diff);
                    return 1;
                }
            }
        }
    }

    printf("Success!\n");

    return 0;
}
