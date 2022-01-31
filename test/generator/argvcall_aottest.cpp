#include "HalideBuffer.h"
#include "HalideRuntime.h"

#include <math.h>
#include <stdio.h>

#include "argvcall.h"

using namespace Halide::Runtime;

const int kSize = 32;

void verify(const Buffer<int32_t, 3> &img, float f1, float f2) {
    for (int i = 0; i < kSize; i++) {
        for (int j = 0; j < kSize; j++) {
            for (int c = 0; c < 3; c++) {
                int expected = (int32_t)(c * (i > j ? i : j) * f1 / f2);
                if (img(i, j, c) != expected) {
                    printf("img[%d, %d, %d] = %d (expected %d)\n", i, j, c, img(i, j, c), expected);
                    exit(-1);
                }
            }
        }
    }
}

int main(int argc, char **argv) {

    int result;
    Buffer<int32_t, 3> output(kSize, kSize, 3);

    // We can, of course, pass whatever values for Param/ImageParam that we like.
    result = argvcall(1.2f, 3.4f, output);
    if (result != 0) {
        fprintf(stderr, "Result: %d\n", result);
        exit(-1);
    }
    verify(output, 1.2f, 3.4f);

    // verify that calling via the _argv entry point
    // also produces the correct result
    float arg0 = 1.234f;
    float arg1 = 3.456f;
    void *args[3] = {&arg0, &arg1, (halide_buffer_t *)output};
    result = argvcall_argv(args);
    if (result != 0) {
        fprintf(stderr, "Result: %d\n", result);
        exit(-1);
    }
    verify(output, arg0, arg1);

    printf("Success!\n");
    return 0;
}
