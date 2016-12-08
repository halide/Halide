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
    Func f;
    Var x, y;

    // Function mixes type, the float16_t should be implicitly upcast
    // to a float
    f(x, y) = 0.25f + Expr(float16_t(0.75));

    Buffer<float> simple = f.realize(10, 3);

    // Read result back
    for (int y = 0; y < 3; y++) {
        for (int x = 0; x < 10; x++) {
            h_assert(simple(x, y) == 1.0f, "Invalid value read back");
        }
    }
    printf("Success!\n");
    return 0;
}
