#include "Halide.h"
#include <stdio.h>
#include <cmath>

using namespace Halide;

// EURGH: Why aren't we using a unit test framework for this?
void h_assert(bool condition, const char *msg) {
    if (!condition) {
        printf("FAIL: %s\n", msg);
        abort();
    }
}

int main() {
    Halide::Func f;
    Halide::Var x, y;

    // Casting to float16_t without specifying a rounding mode
    // is an error
    f(x, y) = cast<float16_t>(0.25f);

    // Use JIT for computation
    printf("Should not be reached!\n");
    return 0;
}
