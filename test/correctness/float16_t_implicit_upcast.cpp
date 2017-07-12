#include "Halide.h"
#include <stdio.h>
#include <cmath>

using namespace Halide;

// FIXME: We should use a proper framework for this. See issue #898
void h_assert(bool condition, const char *msg) {
    if (!condition) {
        printf("FAIL: %s\n", msg);
        abort();
    }
}

int main() {
    Halide::Func f;
    Halide::Var x, y;

    // Function mixes type, the float16_t should be implicitly upcast
    // to a float
    f(x, y) = 0.25f + Expr(float16_t(0.75));

    // Use JIT for computation
    Buffer<float> simple = f.realize(10, 3);

    // Assert some basic properties of the image
    h_assert(simple.extent(0) == 10, "invalid width");
    h_assert(simple.extent(1) == 3, "invalid height");
    h_assert(simple.min(0) == 0, "unexpected non zero min in x");
    h_assert(simple.min(1) == 0, "unexpected non zero min in y");
    h_assert(simple.channels() == 1, "invalid channels");
    h_assert(simple.dimensions() == 2, "invalid number of dimensions");

    // Read result back
    for (int x = simple.min(0); x < simple.extent(0); ++x) {
        for (int y = simple.min(1); y < simple.extent(1); ++y) {
            h_assert(simple(x, y) == 1.0f, "Invalid value read back");
        }
    }
    printf("Success!\n");
    return 0;
}
