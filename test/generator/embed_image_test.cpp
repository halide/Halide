#include <math.h>
#include <stdio.h>

#include "embed_image.h"
#include "static_image.h"

int main(int argc, char **argv) {
    Image<float> input(10, 10, 3);
    for (int y = 0; y < 10; y++) {
        for (int x = 0; x < 10; x++) {
            input(x, y, 0) = sinf(x*y+1);
            input(x, y, 1) = cosf(x*y+1);
            input(x, y, 2) = sqrtf(x*x+y*y);
        }
    }
    Image<float> output(10, 10, 3);

    embed_image(input, output);

    // We expected the color channels to be flipped and multiplied by 0.5
    for (int y = 0; y < 10; y++) {
        for (int x = 0; x < 10; x++) {
            for (int c = 0; c < 3; c++) {
                float correct = input(x, y, 2-c) * 0.5f;
                if (fabs(output(x, y, c) - correct) > 0.0001f) {
                    printf("output(%d, %d, %d) was %f instead of %f\n",
                           x, y, c, output(x, y, c), correct);
                }
            }
        }
    }

    printf("Success!\n");
    return 0;
}
