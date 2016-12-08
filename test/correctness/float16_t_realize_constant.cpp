#include "Halide.h"
#include <stdio.h>
#include <cmath>

using namespace Halide;

void h_assert(bool condition, const char *msg) {
    if (!condition) {
        printf("FAIL: %s\n", msg);
        abort();
    }
}

int main() {
    Halide::Func f;
    Halide::Var x, y;

    // Function simply writes a constant
    f(x, y) = float16_t(0.75);

    // Make sure tracing works. It should abort if something is wrong.
    f.trace_stores();

    // Use JIT for computation
    h_assert(sizeof(float16_t) == 2, "float16_t has invalid size");
    Buffer<float16_t> simple = f.realize(10, 3);

    // Read result back
    for (int y = 0; y < 3; y++) {
        for (int x = 0; x < 10; x++) {
            h_assert(simple(x, y) == float16_t(0.75), "Invalid value read back");
            h_assert(simple(x, y).to_bits() == 0x3a00, "Bit pattern incorrect");
        }
    }

    printf("Success!\n");
    return 0;
}
