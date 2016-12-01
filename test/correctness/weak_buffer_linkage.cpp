#include <stdio.h>

#include "HalideBuffer.h"

// This test exists to verify that Halide::Buffer can be used without
// any Halide runtime being present; for this to work, halide_weak_device_free()
// must be weak, which is straightforward enough on Linux... but on Darwin,
// you must also explicitly mark it as undefined via "-Wl,-U,_halide_weak_device_free"
// as an argument to the linker.
int main(int argc, char **argv) {
    Halide::Buffer<uint8_t, 3> buffer;
    // Ensure it isn't optimized away
    printf("Buffer width is: %d\n", buffer.extent(0));
    printf("Success!");
    return 0;
}
