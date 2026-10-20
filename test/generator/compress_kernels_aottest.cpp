#include "HalideBuffer.h"
#include "HalideRuntime.h"
#include <stdio.h>

#include "compress_kernels.h"

using namespace Halide::Runtime;

bool run_and_check(Buffer<int, 2> &input, const halide_device_interface_t **device_interface) {
    Buffer<int, 2> output(input.width(), input.height());
    if (compress_kernels(input, output) != 0) {
        printf("compress_kernels() failed\n");
        return false;
    }
    *device_interface = output.raw_buffer()->device_interface;
    output.copy_to_host();
    for (int y = 0; y < output.height(); y++) {
        for (int x = 0; x < output.width(); x++) {
            if (output(x, y) != input(x, y) * 2 + 3) {
                printf("Error at %d, %d: %d != %d\n", x, y, output(x, y), input(x, y) * 2 + 3);
                return false;
            }
        }
    }
    return true;
}

int main(int argc, char **argv) {
    Buffer<int, 2> input(64, 48);
    input.for_each_element([&](int x, int y) { input(x, y) = x + y * 100; });

    const halide_device_interface_t *device_interface = nullptr;
    if (!run_and_check(input, &device_interface)) {
        return 1;
    }

    if (device_interface) {
        // Releasing the device drops the compiled kernels, so the next call
        // has to compile them again from the decompressed source.
        input.device_free();
        halide_device_release(nullptr, device_interface);
        if (!run_and_check(input, &device_interface)) {
            return 1;
        }
    } else {
        printf("No GPU target enabled; only the host path was tested.\n");
    }

    printf("Success!\n");
    return 0;
}
