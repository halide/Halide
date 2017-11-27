#include <stdio.h>
#include "Halide.h"
#include "HalideRuntimeMetal.h"
#include <iostream>

using namespace Halide;

extern "C" {
int total_calls = 0;

int halide_metal_acquire_command_buffer(halide_metal_device *device,
                                         halide_metal_command_queue *queue,
                                         halide_metal_command_buffer **buffer_ret) {
    return -1;
}

int halide_metal_release_command_buffer(halide_metal_device *device,
                                        halide_metal_command_queue *queue,
                                        halide_metal_command_buffer *buffer,
                                        bool must_release) {
    return -1;
}
} // extern "C"

int main(int argc, char **argv) {

    Var x("x"), y("y"), xi("xi"), yi("yi");
    Func f("f");

    f(x, y) = x*y + 2.4f;

    Target target = get_jit_target_from_environment();
    if (target.has_gpu_feature() && target.has_feature(Target::Metal)) {
        f.gpu_tile(x, y, xi, yi, 8, 8);


        Buffer<float> imf = f.realize(32, 32, target);

        // Check the result was what we expected
        for (int i = 0; i < 32; i++) {
            for (int j = 0; j < 32; j++) {
                float correct = i*j + 2.4f;
                if (fabs(imf(i, j) - correct) > 0.001f) {
                    printf("imf[%d, %d] = %f instead of %f\n", i, j, imf(i, j), correct);
                    return -1;
                }
            }
        }
    }

    printf("Success!\n");
    return 0;
}
