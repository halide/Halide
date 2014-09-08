#include <Halide.h>
#include <stdio.h>
#include <algorithm>
#include <stdlib.h>

using namespace Halide;

#define CHECK_EQ(a, b)                                                  \
    do {                                                                \
        if ((a) != (b)) {                                               \
            fprintf(stderr, "Error: %s != %s for y=%d. Actual values: %d != %d\n", #a, #b, y, a, b); \
            return 1;                                                   \
        }                                                               \
    } while(false)

int main() {
    // This test must be run with an OpenGL target
    const Target &target = get_jit_target_from_environment();
    if (!target.has_feature(Target::OpenGL))  {
        fprintf(stderr,"ERROR: This test must be run with an OpenGL target, e.g. by setting HL_JIT_TARGET=host-opengl.\n");
        return 1;
    }

    Func f;
    Var x, y, c;

    Expr e = 0;

    // Max with integer arguments requires Halide to introduce an implicit
    // cast to float.
    e = select(x == 0, max(y, 5), e);
    // But using float directly should also work.
    e = select(x == 1, cast<int>(min(cast<float>(y), 5.0f)), e);

    e = select(x == 2, y % 3, e);
    e = select(x == 3, cast<int>(127*sin(y) + 128), e);
    e = select(x == 4, y / 2, e);

    f(x, y, c) = cast<uint8_t>(e);

    Image<uint8_t> out(10, 10, 1);
    f.bound(c, 0, 1);
    f.glsl(x, y, c);
    f.realize(out);

    out.copy_to_host();

    for (int y = 0; y < out.height(); y++) {
        CHECK_EQ(out(0, y, 0), std::max(y, 5));
        CHECK_EQ(out(1, y, 0), std::min(y, 5));
        CHECK_EQ(out(2, y, 0), y % 3);
        CHECK_EQ(out(3, y, 0), static_cast<int>(127*std::sin(y) + 128));
        CHECK_EQ(out(4, y, 0), y / 2);
    }

    printf("Success!\n");
    return 0;
}
