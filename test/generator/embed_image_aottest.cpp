#include <math.h>
#include <stdio.h>

#include "HalideBuffer.h"
#include "embed_image.h"

using namespace Halide::Runtime;

int main(int argc, char **argv) {
    Buffer<float, 3> input(10, 10, 3);
    for (int y = 0; y < 10; y++) {
        for (int x = 0; x < 10; x++) {
            input(x, y, 0) = sinf((float)(x * y + 1));
            input(x, y, 1) = cosf((float)(x * y + 1));
            input(x, y, 2) = sqrtf((float)(x * x + y * y));
        }
    }
    Buffer<float, 3> output(10, 10, 3);

    embed_image(input, output);

    // We expected the color channels to be flipped and multiplied by 0.5
    for (int y = 0; y < 10; y++) {
        for (int x = 0; x < 10; x++) {
            for (int c = 0; c < 3; c++) {
                float correct = input(x, y, 2 - c) * 0.5f;
                if (fabs(output(x, y, c) - correct) > 0.0001f) {
                    printf("output(%d, %d, %d) was %f instead of %f\n", x, y, c, output(x, y, c),
                           correct);
                }
            }
        }
    }

    printf("Success!\n");
    return 0;
}
