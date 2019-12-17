#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "HalideBuffer.h"
#include "HalideRuntime.h"
#include "HalideRuntimeOpenGL.h"

using Halide::Runtime::Buffer;

#include "halide_blur_glsl.h"
#include "halide_ycc_glsl.h"

void test_blur() {
    const int W = 12, H = 32, C = 3;
    Buffer<uint8_t> input(W, H, C);
    Buffer<uint8_t> output(W, H, C);

    fprintf(stderr, "test_blur\n");
    halide_blur_glsl(input, output);
    fprintf(stderr, "test_blur complete\n");
}

void test_ycc() {
    const int W = 12, H = 32, C = 3;
    Buffer<uint8_t> input(W, H, C);
    Buffer<uint8_t> output(W, H, C);

    fprintf(stderr, "test_ycc\n");
    halide_ycc_glsl(input, output);
    fprintf(stderr, "Ycc complete\n");
}

void test_device_sync() {
    const int W = 12, H = 32, C = 3;
    Buffer<uint8_t> temp(W, H, C);

    temp.set_host_dirty();
    int result = temp.copy_to_device(halide_opengl_device_interface());
    if (result != 0) {
        fprintf(stderr, "halide_device_malloc failed with return %d.\n", result);
        abort();
    } else {
        result = temp.device_sync();
        if (result != 0) {
            fprintf(stderr, "halide_device_sync failed with return %d.\n", result);
            abort();
        } else {
            fprintf(stderr, "Test device sync complete.\n");
        }
    }
}

int main(int argc, char *argv[]) {
    test_blur();
    test_ycc();
    test_device_sync();
}
