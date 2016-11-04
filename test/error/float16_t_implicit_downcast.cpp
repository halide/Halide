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

    // This should throw an error because downcasting will lose precision which
    // should only happen if the user explicitly asks for it
    f(x, y) = 0.1f;

    // Use JIT for computation
    Buffer<float16_t> simple = f.realize(10, 3);

    // Assert some basic properties of the image
    h_assert(simple.extent(0) == 10, "invalid width");
    h_assert(simple.extent(1) == 3, "invalid height");
    h_assert(simple.min(0) == 0, "unexpected non zero min in x");
    h_assert(simple.min(1) == 0, "unexpected non zero min in y");
    h_assert(simple.channels() == 1, "invalid channels");
    h_assert(simple.dimensions() == 2, "invalid number of dimensions");

    printf("Should not be reached!\n");
    return 0;
}
