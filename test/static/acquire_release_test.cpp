#include <acquire_release.h>
#include <static_image.h>
#include <math.h>
#include <stdio.h>
#include <HalideRuntime.h>
#include <assert.h>

const int W = 256, H = 256;

int main(int argc, char **argv) {
    Image<float> input(W, H);
    for (int y = 0; y < input.height(); y++) {
        for (int x = 0; x < input.width(); x++) {
            input(x, y) = (float)(x * y);
        }
    }
    Image<float> output(W, H);

    acquire_release(input, output);

    for (int y = 0; y < output.height(); y++) {
        for (int x = 0; x < output.width(); x++) {
            if (input(x, y) * 2.0f != output(x, y)) {
                printf("Error at (%d, %d): %f != %f\n", x, y, input(x, y) * 2.0f, output(x, y));
                return -1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
