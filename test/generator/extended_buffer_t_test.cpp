#include <math.h>
#include <stdio.h>

#include "extended_buffer_t_common.h"
#include "extended_buffer_t.h"
#include "static_image.h"

int main(int argc, char **argv) {
    Image<float> input(10, 10);
    for (int y = 0; y < 10; y++) {
        for (int x = 0; x < 10; x++) {
            input(x, y) = sinf(x * y + 1);
        }
    }
    Image<float> output(10, 10, 3);

    fancy_buffer_t fancy_input(input);
    fancy_input.extra_field = 17;

    extended_buffer_t(&fancy_input, output);

    // Output should be input + 17
    for (int y = 0; y < 10; y++) {
        for (int x = 0; x < 10; x++) {
            float correct = input(x, y) + 17;
            if (fabs(output(x, y) - correct) > 0.0001f) {
                printf("output(%d, %d) was %f instead of %f\n", x, y, output(x, y), correct);
                return -1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
