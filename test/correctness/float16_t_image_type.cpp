#include "Halide.h"
#include <stdio.h>
#include <cmath>

using namespace Halide;

int main() {
    Halide::Buffer<float16_t> im(10, 3);
    im.set_min(4, -6);

    // Write a constant value and check we can read it back. Mostly
    // this checks the addressing math is doing the right thing for
    // float16_t.
    im.for_each_element([&](int x, int y) {
            im(x, y) = float16_t(x + y / 8.0);
        });

    if (im.size_in_bytes() != im.number_of_elements() * 2) {
        printf("Incorrect amount of memory allocated\n");
        return -1;
    }

    for (int y = im.dim(1).min(); y <= im.dim(1).max(); y++) {
        for (int x = im.dim(0).min(); x <= im.dim(0).max(); x++) {
            float correct = x + y / 8.0f;
            float actual = (float)im(x, y);
            if (correct != actual) {
                printf("im(%d, %d) = %f instead of %f\n",
                       x, y, actual, correct);
                return -1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
