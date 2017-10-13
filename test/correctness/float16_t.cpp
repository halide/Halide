#include "Halide.h"
#include <stdio.h>
#include <cmath>

using namespace Halide;

int main() {
    // Check we can send a float16_t through the compiler with some simple arithmetic.
    {
        Param<float16_t> p;
        p.set(float16_t(0.75));

        float16_t val = evaluate<float16_t>(p);

        if ((float)val != 0.75f) {
            printf("Expected 0.75, got %f\n", (float)val);
        }
    }

    {
        // A float16_t should be implicitly upcast to a float
        float val = evaluate<float>(0.25f + Expr(float16_t(0.75)));

        if (val != 1.0f) {
            printf("Expected 1, got %f\n", val);
            return -1;
        }
    }

    printf("Success!\n");
    return 0;
}
